use std::cmp::Ordering;
use std::ffi::{c_char, c_int, c_void, CStr, CString};

pub const HEAD_LENGTH: usize = 16;
pub const ATTR_TAG_LANGTYPE: u8 = 1;

pub const OK: c_int = 0;
pub const MISSING: c_int = -2;
pub const NOT_INDEX: c_int = -20;
pub const NO_IN_PROCESS_ADDRESS: c_int = -5;

#[repr(C)]
pub struct Anchor {
    pub node_offset: u64,
    pub epoch: u64,
    pub depth: u32,
    pub flags: u32,
}

extern "C" {
    fn kvspaceConnect(data_source_name: *const c_char) -> *mut c_void;
    fn kvspaceClose(handle: *mut c_void);
    fn kvspaceXvalueInProcessAddress(handle: *mut c_void) -> *mut u8;
    fn kvspaceResolveOffset(
        handle: *mut c_void,
        key: *const c_char,
        anchor: *const Anchor,
        out_value_offset: *mut u64,
    ) -> c_int;
    fn kvspaceFree(handle: *mut c_void, key: *const c_char) -> c_int;
    fn kvspaceCp(handle: *mut c_void, old_key: *const c_char, new_key: *const c_char) -> c_int;
    fn kvspaceListlen(
        handle: *mut c_void,
        key: *const c_char,
        out_count: *mut i64,
        out_max_name_length: *mut u32,
    ) -> c_int;
    fn kvspaceList(
        handle: *mut c_void,
        key: *const c_char,
        out_names: *mut c_char,
        out_capacity: u32,
        out_length: *mut u32,
        out_count: *mut u32,
        out_generation: *mut u64,
    ) -> c_int;
    fn kvspaceXvalueGetAttr(
        wire: *const u8,
        wire_length: u64,
        tag: u8,
        out_data: *mut *const u8,
        out_length: *mut u16,
    ) -> c_int;
    fn kvlangXvalueNewT(
        handle: *mut c_void,
        key: *const c_char,
        raw: *const u8,
        raw_length: u32,
        langtype: *const c_char,
    ) -> *mut u8;
    fn kvlangXvalueTextToBytes(
        langtype: *const c_char,
        langtype_length: usize,
        value_text: *const c_char,
        value_text_length: usize,
        output: *mut u8,
        output_capacity: usize,
        output_length: *mut usize,
        failure: *mut c_int,
    ) -> bool;
    fn kvlangXvalueTextFromBytes(
        langtype: *const c_char,
        langtype_length: usize,
        body: *const u8,
        body_length: usize,
        output: *mut c_char,
        output_capacity: usize,
        output_length: *mut usize,
        failure: *mut c_int,
    ) -> bool;
    fn kvlangXvalueTextFailureText(failure: c_int) -> *const c_char;
}

pub struct Store {
    handle: *mut c_void,
}

impl Store {
    pub fn connect(data_source_name: &str) -> Option<Store> {
        let name = CString::new(data_source_name).ok()?;
        let handle = unsafe { kvspaceConnect(name.as_ptr()) };
        (!handle.is_null()).then_some(Store { handle })
    }

    pub fn resolve(&self, key: &str) -> Result<Option<Vec<u8>>, c_int> {
        let name = CString::new(key).expect("key contains NUL");
        let mut value_offset: u64 = 0;
        let status =
            unsafe { kvspaceResolveOffset(self.handle, name.as_ptr(), std::ptr::null(), &mut value_offset) };
        if status == MISSING {
            return Ok(None);
        }
        if status != OK {
            return Err(status);
        }
        let base = unsafe { kvspaceXvalueInProcessAddress(self.handle) };
        if base.is_null() {
            return Err(NO_IN_PROCESS_ADDRESS);
        }
        let wire = unsafe { std::slice::from_raw_parts(base.add(value_offset as usize), HEAD_LENGTH) };
        let body_length = u64::from_le_bytes(wire[0..8].try_into().unwrap());
        let attribute_length = u16::from_le_bytes([wire[8], wire[9]]) as u64;
        let total = HEAD_LENGTH as u64 + body_length + attribute_length;
        let bytes = unsafe {
            std::slice::from_raw_parts(base.add(value_offset as usize), total as usize).to_vec()
        };
        Ok(Some(bytes))
    }

    pub fn delete(&self, key: &str) -> c_int {
        let name = CString::new(key).expect("key contains NUL");
        unsafe { kvspaceFree(self.handle, name.as_ptr()) }
    }

    pub fn copy(&self, source: &str, destination: &str) -> c_int {
        let old_key = CString::new(source).expect("key contains NUL");
        let new_key = CString::new(destination).expect("key contains NUL");
        unsafe { kvspaceCp(self.handle, old_key.as_ptr(), new_key.as_ptr()) }
    }

    pub fn list(&self, prefix: &str) -> Result<Vec<String>, c_int> {
        let name = CString::new(prefix).expect("key contains NUL");
        let mut count: i64 = 0;
        let mut max_name_length: u32 = 0;
        let status = unsafe {
            kvspaceListlen(self.handle, name.as_ptr(), &mut count, &mut max_name_length)
        };
        if status == MISSING || status == NOT_INDEX {
            return Ok(Vec::new());
        }
        if status != OK {
            return Err(status);
        }
        if count == 0 {
            return Ok(Vec::new());
        }
        let capacity = (count as u32).saturating_mul(max_name_length.saturating_add(1));
        let mut buffer = vec![0u8; capacity as usize];
        let mut length: u32 = 0;
        let mut listed: u32 = 0;
        let mut generation: u64 = 0;
        let status = unsafe {
            kvspaceList(
                self.handle,
                name.as_ptr(),
                buffer.as_mut_ptr() as *mut c_char,
                capacity,
                &mut length,
                &mut listed,
                &mut generation,
            )
        };
        if status != OK {
            return Err(status);
        }
        let mut names = Vec::new();
        let mut start = 0usize;
        for index in 0..length as usize {
            if buffer[index] == 0 {
                if index > start {
                    names.push(String::from_utf8_lossy(&buffer[start..index]).into_owned());
                }
                start = index + 1;
            }
        }
        names.sort_by(|left, right| key_order(left, right));
        Ok(names)
    }

    pub fn langtype(&self, wire: &[u8]) -> Option<Vec<u8>> {
        let mut data: *const u8 = std::ptr::null();
        let mut length: u16 = 0;
        let status = unsafe {
            kvspaceXvalueGetAttr(
                wire.as_ptr(),
                wire.len() as u64,
                ATTR_TAG_LANGTYPE,
                &mut data,
                &mut length,
            )
        };
        if status != OK || data.is_null() || length == 0 {
            return None;
        }
        Some(unsafe { std::slice::from_raw_parts(data, length as usize) }.to_vec())
    }

    pub fn head_fields(wire: &[u8]) -> (u64, u16) {
        let body_length = u64::from_le_bytes(wire[0..8].try_into().unwrap());
        let attribute_length = u16::from_le_bytes([wire[8], wire[9]]);
        (body_length, attribute_length)
    }

    pub fn body(wire: &[u8]) -> &[u8] {
        let (body_length, _) = Self::head_fields(wire);
        let end = HEAD_LENGTH + body_length as usize;
        &wire[HEAD_LENGTH..end.min(wire.len())]
    }

    pub fn write_text(&self, key: &str, langtype: &str, text: &str) -> Result<(), String> {
        let name = CString::new(key).map_err(|_| "key contains NUL".to_string())?;
        let language_type = CString::new(langtype).map_err(|_| "langtype contains NUL".to_string())?;
        let value_text = CString::new(text).map_err(|_| "value contains NUL".to_string())?;
        let mut failure: c_int = 0;
        let mut body_length: usize = 0;
        let first = unsafe {
            kvlangXvalueTextToBytes(
                language_type.as_ptr(),
                langtype.len(),
                value_text.as_ptr(),
                text.len(),
                std::ptr::null_mut(),
                0,
                &mut body_length,
                &mut failure,
            )
        };
        if !first {
            return Err(failure_text(failure));
        }
        let mut body = vec![0u8; body_length];
        let second = unsafe {
            kvlangXvalueTextToBytes(
                language_type.as_ptr(),
                langtype.len(),
                value_text.as_ptr(),
                text.len(),
                body.as_mut_ptr(),
                body.len(),
                &mut body_length,
                &mut failure,
            )
        };
        if !second {
            return Err(failure_text(failure));
        }
        if body_length > u32::MAX as usize {
            return Err("body exceeds 4GiB".to_string());
        }
        let wire = unsafe {
            kvlangXvalueNewT(
                self.handle,
                name.as_ptr(),
                body.as_ptr(),
                body_length as u32,
                language_type.as_ptr(),
            )
        };
        if wire.is_null() {
            return Err("write failed".to_string());
        }
        Ok(())
    }

    pub fn read_text(&self, key: &str) -> Result<Option<(String, String)>, String> {
        let wire = match self.resolve(key) {
            Ok(Some(wire)) => wire,
            Ok(None) => return Ok(None),
            Err(status) => return Err(format!("resolve failed: {status}")),
        };
        let langtype = self
            .langtype(&wire)
            .ok_or_else(|| "key has no langtype record".to_string())?;
        let body = Self::body(&wire);
        let mut failure: c_int = 0;
        let mut text_length: usize = 0;
        let first = unsafe {
            kvlangXvalueTextFromBytes(
                langtype.as_ptr() as *const c_char,
                langtype.len(),
                body.as_ptr(),
                body.len(),
                std::ptr::null_mut(),
                0,
                &mut text_length,
                &mut failure,
            )
        };
        if !first {
            return Err(failure_text(failure));
        }
        let mut output = vec![0u8; text_length + 1];
        let second = unsafe {
            kvlangXvalueTextFromBytes(
                langtype.as_ptr() as *const c_char,
                langtype.len(),
                body.as_ptr(),
                body.len(),
                output.as_mut_ptr() as *mut c_char,
                output.len(),
                &mut text_length,
                &mut failure,
            )
        };
        if !second {
            return Err(failure_text(failure));
        }
        output.truncate(text_length);
        Ok(Some((
            String::from_utf8_lossy(&langtype).into_owned(),
            String::from_utf8_lossy(&output).into_owned(),
        )))
    }
}

impl Drop for Store {
    fn drop(&mut self) {
        unsafe { kvspaceClose(self.handle) }
    }
}

fn failure_text(failure: c_int) -> String {
    let text = unsafe { kvlangXvalueTextFailureText(failure) };
    if text.is_null() {
        return format!("text conversion failed: {failure}");
    }
    unsafe { CStr::from_ptr(text) }.to_string_lossy().into_owned()
}

fn is_coordinate(name: &str) -> bool {
    let bytes = name.as_bytes();
    if bytes.len() < 3 || bytes[0] != b'[' || bytes[bytes.len() - 1] != b']' {
        return false;
    }
    !bytes[1..bytes.len() - 1]
        .iter()
        .any(|byte| *byte == b'[' || *byte == b']')
}

fn parse_coordinate(name: &str) -> Option<Vec<i64>> {
    let bytes = name.as_bytes();
    if bytes.first() != Some(&b'[') {
        return None;
    }
    let mut coordinates = Vec::new();
    let mut current: i64 = 0;
    let mut has_digits = false;
    for &byte in &bytes[1..] {
        if byte.is_ascii_digit() {
            current = current * 10 + (byte - b'0') as i64;
            has_digits = true;
        } else if byte == b',' {
            if !has_digits {
                return None;
            }
            coordinates.push(current);
            current = 0;
            has_digits = false;
        } else if byte == b']' {
            if !has_digits {
                return None;
            }
            coordinates.push(current);
            return Some(coordinates);
        } else {
            return None;
        }
    }
    None
}

pub fn key_order(left: &str, right: &str) -> Ordering {
    let left_is_coordinate = is_coordinate(left);
    let right_is_coordinate = is_coordinate(right);
    match (left_is_coordinate, right_is_coordinate) {
        (false, false) => left.cmp(right),
        (false, true) => Ordering::Greater,
        (true, false) => Ordering::Less,
        (true, true) => {
            let left_coordinates = parse_coordinate(left);
            let right_coordinates = parse_coordinate(right);
            match (left_coordinates, right_coordinates) {
                (Some(left_values), Some(right_values)) => {
                    for (left_value, right_value) in left_values.iter().zip(right_values.iter()) {
                        if left_value != right_value {
                            return left_value.cmp(right_value);
                        }
                    }
                    left_values.len().cmp(&right_values.len())
                }
                _ => left.cmp(right),
            }
        }
    }
}
