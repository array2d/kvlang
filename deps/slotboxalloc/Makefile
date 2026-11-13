.PHONY: build debug asan tsan ubsan test clean deb

build:
	@mkdir -p build && cd build && cmake -DCMAKE_BUILD_TYPE=Release .. && make -j$$(nproc)

debug:
	@mkdir -p build && cd build && cmake -DCMAKE_BUILD_TYPE=Debug .. && make -j$$(nproc)

asan:
	@mkdir -p build && cd build && cmake -DCMAKE_BUILD_TYPE=Debug \
		-DCMAKE_C_FLAGS="-fsanitize=address -fno-omit-frame-pointer -g" \
		-DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address" \
		-DCMAKE_SHARED_LINKER_FLAGS="-fsanitize=address" .. && make -j$$(nproc)

tsan:
	@mkdir -p build && cd build && cmake -DCMAKE_BUILD_TYPE=Debug \
		-DCMAKE_C_FLAGS="-fsanitize=thread -fno-omit-frame-pointer -g" \
		-DCMAKE_EXE_LINKER_FLAGS="-fsanitize=thread" \
		-DCMAKE_SHARED_LINKER_FLAGS="-fsanitize=thread" .. && make -j$$(nproc)

ubsan:
	@mkdir -p build && cd build && cmake -DCMAKE_BUILD_TYPE=Debug \
		-DCMAKE_C_FLAGS="-fsanitize=undefined -fno-omit-frame-pointer -g" \
		-DCMAKE_EXE_LINKER_FLAGS="-fsanitize=undefined" \
		-DCMAKE_SHARED_LINKER_FLAGS="-fsanitize=undefined" .. && make -j$$(nproc)

test: build
	@cd build && ctest

deb:
	dpkg-buildpackage -us -uc -b

clean:
	rm -rf build
