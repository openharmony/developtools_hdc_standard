# HDC-OpenHarmony Device Connector<a name="EN-US_TOPIC_0000001149090043"></a>

- [HDC-OpenHarmony Device Connector<a name="EN-US_TOPIC_0000001149090043"></a>](#hdc-OpenHarmony-Device-Connector)
  - [Introduction<a name="section662115419449"></a>](#introduction)
  - [Architecture<a name="section15908143623714"></a>](#architecture)
  - [Table of Contents<a name="section161941989596"></a>](#table-of-contents)
    - [PC-side compilation instructions<a name="section129654513262"></a>](#pc-side-compilation-instructions)
    - [Getting instructions on pc<a name="section129654513263"></a>](#getting-instructions-on-pc)
  - [More help and docs<a name="section129654513264"></a>](#more-help-and-docs)

## Introduction<a name="section662115419449"></a>

HDC (OpenHarmony Device Connector) is a command-line tool for developers to connect and debug the device. The PC-side development machine uses the command-line tool hdc_std (for convenience, collectively referred to as hdc below). This tool needs to support deployment on Windows/Linux /Mac and other systems to connect and debug communication with OpenHarmony devices (or simulators). The PC-side hdc tool needs to release corresponding versions for the above development machine operating system platforms, and the device-side hdc daemon needs to follow the device image release including support for the simulator. The following will introduce the commonly used commands and usage examples of hdc.

## Architecture<a name="section15908143623714"></a>

HDC mainly consists of three parts:

1. The hdc client part: the client running on the development machine, the user can request to execute the corresponding hdc command under the command terminal of the development machine (windows cmd/linux shell), running on the development machine, other terminal debugging IDEs also include hdc client .

2. The hdc server part: As a background process, it also runs on the development machine. The server manages the communication between the client and the device-side daemon, including the multiplexing of connections, the sending and receiving of data communication packets, and the direct processing of individual local commands.

3. The hdc daemon part: the daemon is deployed on the OpenHarmony device running on demand, and is responsible for processing requests from the client side.

## Table of Contents<a name="section161941989596"></a>

```
/developtools
├── hdc_standard # hdc code directory
│ └── src
│ ├── common # Code directory shared by the device side and the host side
│ ├── daemon # Code directory on the device side
│ ├── host # The code directory of the host
│ └── test # Code directory of test case
```

### PC-side compilation instructions<a name="section129654513262"></a>


Compilation steps of hdc pc executable file:

1. Compile command: Please refer to https://gitee.com/openharmony/build/blob/master/README_zh.md to compile the sdk instructions, execute the specified sdk compile command to compile the entire sdk, hdc will be compiled Pack it inside.

2. Compile: Run the sdk compilation command adjusted above on the target development machine, and the normal compilation of hdc_std will be output to the relevant directory of the sdk platform; Note: Only the windows/linux version tools can be compiled in the ubuntu environment, and the mac version needs to be on the macos development machine Compile.


### Getting instructions on pc<a name="section129654513263"></a>

[1. Download sdk to obtain (recommended)](#section161941989591)
```
Download the dailybuilds or officially released sdk compressed package by visiting the community website, and unzip and extract it according to your platform to the corresponding directory toolchain
```

[2. Compile by yourself](#section161941989592)

For compilation, please refer to the separate section above. Prebuilt is no longer available in the prebuilt directory of this project warehouse.


[3. Support operating environment](#section161941989593)

The linux version is recommended to be 64-bit above ubuntu 16.04, and other similar versions are also available; libc++.so quotes errors, please use ldd/readelf and other commands to check the library. Windows version is recommended. Windows 10 64-bit is recommended. If the lower version of the windows winusb library is missing, please use zadig to update the library. . 

## More help and docs<a name="section129654513264"></a>

Please check the Chinese description file ‘README_zh.md’ or raise an issue in the gitgee community.