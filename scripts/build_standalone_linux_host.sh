#!/bin/bash
# Copyright (C) 2021 Huawei Device Co., Ltd.
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# gcc-9 needed

ohos_root=$1
ohos_hdc_build="ohos_hdc_build"
cwddir=`pwd`

build_in_source=true
[ "X$ohos_root" == "X" ] && build_in_source=false

if [ $build_in_source == "true" ] ; then
	ohos_root_real=$(realpath $ohos_root)
fi

[ "X$KEEP" == "X" ] && [ -d $ohos_hdc_build ] && rm -fr $ohos_hdc_build
[ -d $ohos_hdc_build ] || mkdir $ohos_hdc_build

STATICLIB=""
INCLUDES=""

function build_libusb ()
{
	libusb_install=$(realpath libusb)
	[ "X$KEEP" == "X" ] && mkdir -pv ${libusb_install}/include && ln -svf /usr/include/libusb-1.0 ${libusb_install}/include/libusb
	INCLUDES+="-I$(realpath ${libusb_install}/include) "
}

function build_openssl ()
{
	pushd third_party_openssl
	[ "X$KEEP" == "X" ] && ./Configure no-shared linux-generic64 && make
	STATICLIB+="$(realpath libcrypto.a) "
	INCLUDES+="-I$(realpath include) "
	popd
}

function build_libuv ()
{
	pushd third_party_libuv
	[ "X$KEEP" == "X" ] && cmake . && make
	STATICLIB+="$(realpath libuv_a.a) "
	INCLUDES+="-I$(realpath include) "
	popd
}

function build_securec ()
{
	pushd third_party_bounds_checking_function
	[ "X$KEEP" == "X" ] && gcc src/*.c -I`pwd`/include -c && ar rcs libsecurec.a *.o
	STATICLIB+="$(realpath libsecurec.a) "
	INCLUDES+="-I$(realpath include) "
	popd
}

function build_lz4 ()
{
	pushd third_party_lz4
	[ "X$KEEP" == "X" ] && make liblz4.a
	STATICLIB+="$(realpath lib/liblz4.a) "
	INCLUDES+="-I$(realpath lib) "
	popd
}

function build_hdc ()
{
	pushd developtools_hdc_standard
	echo $STATICLIB
	echo $INCLUDES

	DEFINES="-DHDC_HOST -DHARMONY_PROJECT"
	export LDFLAGS="-Wl,--copy-dt-needed-entries"
	export CXXFLAGS="-std=c++17 -ggdb -O0"

	g++ ${DEFINES} ${CXXFLAGS} ${INCLUDES} $(find src/common/ src/host/ \( -name "*.cpp" -or -name "*.c" \)) -lusb-1.0 -ldl -lpthread $STATICLIB -o hdc_std

	if [ -f hdc_std ]; then
		echo build success
		cp hdc_std $cwddir
	else
		echo build fail
	fi
	popd
}

pushd $ohos_hdc_build

if [ "X$KEEP" == "X" ]; then
	for name in "developtools/hdc_standard" "third_party/libuv" "third_party/openssl" "third_party/bounds_checking_function" "third_party/lz4"; do
		reponame=$(echo $name | sed "s/\//_/g")
		if [ $build_in_source == "true" ] ; then
			cp -ra ${ohos_root_real}/${name} ${reponame} || exit 1
		else
			git clone https://gitee.com/openharmony/${reponame}
		fi
	done
fi

build_openssl
build_libuv
build_securec
build_lz4
build_libusb

build_hdc

popd
