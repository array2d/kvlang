use crate::engine::Engine;

pub fn dispatch(eng: &Engine, op: &str, pc: &str) {
    let x = eng.read_f64(pc, 0);
    let dst = eng.write0(pc);
    match op {
        "math·isnan" => eng.set_xvalue_wire(&dst, "bool", &[x.is_nan() as u8], &[]),
        "math·isinf" => eng.set_xvalue_wire(&dst, "bool", &[x.is_infinite() as u8], &[]),
        "math·isfinite" => eng.set_xvalue_wire(&dst, "bool", &[x.is_finite() as u8], &[]),
        _ => {
            let v = match op {
                "math·sin" => x.sin(),
                "math·cos" => x.cos(),
                "math·tan" => x.tan(),
                "math·asin" => x.asin(),
                "math·acos" => x.acos(),
                "math·atan" => x.atan(),
                "math·atan2" => x.atan2(eng.read_f64(pc, 1)),
                "math·sinh" => x.sinh(),
                "math·cosh" => x.cosh(),
                "math·tanh" => x.tanh(),
                "math·floor" => x.floor(),
                "math·ceil" => x.ceil(),
                "math·round" => x.round(),
                "math·trunc" => x.trunc(),
                "math·log2" => x.log2(),
                "math·log10" => x.log10(),
                "math·hypot" => x.hypot(eng.read_f64(pc, 1)),
                _ => return,
            };
            eng.set_xvalue_wire(&dst, "float64", &v.to_le_bytes(), &[]);
        }
    }
}
