NAME="clang"
VERSION="4.0.0"
PACKAGE="$NAME-$VERSION"

DEPENDS="gcc cmake python"

GCC_PKG=$PREFIX/$(get_property gcc PACKAGE)
CMAKE_PKG=$PREFIX/$(get_property cmake PACKAGE)
PYTHON_PKG=$PREFIX/$(get_property python PACKAGE)

export CC="$GCC_PKG/bin/gcc"
export CXX="$GCC_PKG/bin/g++"
export PATH="$GCC_PKG/bin:$CMAKE_PKG/bin:$PATH"
export LD_LIBRARY_PATH="$GCC_PKG/lib64"

pkg_download() {
	wget "http://releases.llvm.org/$VERSION/llvm-$VERSION.src.tar.xz"
	wget "http://releases.llvm.org/$VERSION/cfe-$VERSION.src.tar.xz"
	wget "http://releases.llvm.org/$VERSION/compiler-rt-$VERSION.src.tar.xz"
	wget "http://releases.llvm.org/$VERSION/libcxx-$VERSION.src.tar.xz"
	wget "http://releases.llvm.org/$VERSION/libcxxabi-$VERSION.src.tar.xz"
	wget "http://releases.llvm.org/$VERSION/openmp-$VERSION.src.tar.xz"
	wget "http://releases.llvm.org/$VERSION/clang-tools-extra-$VERSION.src.tar.xz"
}

pkg_extract() {
	tar xf "llvm-$VERSION.src.tar.xz"
	mv "llvm-$VERSION.src" "$PACKAGE"

	tar xf "cfe-$VERSION.src.tar.xz"
	mv "cfe-$VERSION.src" "$PACKAGE/tools/clang"

	tar xf "compiler-rt-$VERSION.src.tar.xz"
	mv "compiler-rt-$VERSION.src" "$PACKAGE/projects/compiler-rt"

	tar xf "libcxx-$VERSION.src.tar.xz"
	mv "libcxx-$VERSION.src" "$PACKAGE/projects/libcxx"

	tar xf "libcxxabi-$VERSION.src.tar.xz"
	mv "libcxxabi-$VERSION.src" "$PACKAGE/projects/libcxxabi"

	tar xf "openmp-$VERSION.src.tar.xz"
	mv "openmp-$VERSION.src" "$PACKAGE/projects/openmp"

	tar xf "clang-tools-extra-$VERSION.src.tar.xz"
	mv "clang-tools-extra-$VERSION.src" "$PACKAGE/tools/clang/tools/extra"
}

pkg_configure() {
	mkdir build
	cd build
	cmake \
		-DCMAKE_INSTALL_PREFIX="$PREFIX/$PACKAGE" \
		-DCMAKE_BUILD_TYPE=Release \
		-DGCC_INSTALL_PREFIX="$GCC_PKG" \
		-DPYTHON_EXECUTABLE="$PYTHON_PKG/bin/python" \
		..
}

pkg_build() {
	make -j "$SLOTS"
	make -j "$SLOTS" omp
	make -j "$SLOTS" cxx
}

pkg_install() {
	make install
	make install-cxx install-cxxabi
}

pkg_cleanup() {
	rm -rf "$PACKAGE" \
		"llvm-$VERSION.src.tar.xz" \
		"cfe-$VERSION.src.tar.xz" \
		"compiler-rt-$VERSION.src.tar.xz" \
		"libcxx-$VERSION.src.tar.xz" \
		"libcxxabi-$VERSION.src.tar.xz" \
		"openmp-$VERSION.src.tar.xz" \
		"clang-tools-extra-$VERSION.src.tar.xz"
}
