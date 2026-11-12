.PHONY: build debug clean test deb

build:
	@mkdir -p build && cd build && cmake -DCMAKE_BUILD_TYPE=Release .. && make -j$$(nproc)

debug:
	@mkdir -p build && cd build && cmake -DCMAKE_BUILD_TYPE=Debug .. && make -j$$(nproc)

test: build
	@cd build && ctest

deb:
	dpkg-buildpackage -us -uc -b

clean:
	rm -rf build
