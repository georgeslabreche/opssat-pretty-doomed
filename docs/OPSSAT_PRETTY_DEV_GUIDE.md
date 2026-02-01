# OPS-SAT PRETTY Software Development Guide

This document describes how to setup an environment to cross-compile your C experiment, and how to package it for submissions.

Revision history:

- v0.1 | Aug 26, 2025 | Initial version

## Cross-compiling Environment

Tested on prerequisites on Debian 12, but should work on any recent similar distribution with `qemu` available, the following commands may need some adjustment depending on the specific Linux version/distribution.

1. Install qemu-user-static on your Linux distribution:

   ```
   $ sudo apt-get install qemu-user-static
   ```

2. Unpack exp_env.tar.gz to `<env-folder>`, for example:

   ```
   $ tar zxvf exp_env.tar.gz exp_env
   ```

3. Copy name resolution configuration from your system to the extracted environment:

   ```
   $ sudo cp /etc/resolv.conf <env-folder>/etc
   ```

4. Start a bash in the new environment using chroot:

   ```
   $ sudo chroot <env-folder> /bin/sh
   ```

5. Inside the chroot environment install the following packages:

   ```
   exp_env$ apk add gcc musl-dev
   ```

6. Compile the source code inside the chroot environment:

   ```
   exp_env$ gcc -o my-exp my-exp.c
   ```

## Building Package

Build a tar.gz package with all files having relative paths and belonging to user 'exp':

```
$ tar --owner=exp --group=exp -zcvf <my-experiment>.tar.gz <exp-files>
```

Make sure to include an executable script `run`, this is the entrypoint to run the experiment.

The experiment will be executed in the following environment:

- User: `exp`
- Home directory: `$HOME`
  - contains the files in the package.
  - `./run` is the entry point for experiment execution, and needs to be set executable
  - it is writable, and any changes will be collected, and eventually transferred to ground. Therefore, it should be kept as clean as possible on exit.

## Packages List

```
alpine-baselayout
alpine-baselayout-data
alpine-keys
alpine-release
apk-tools
avahi-libs
busybox
busybox-binsh
c-ares
ca-certificates-bundle
dbus-libs
i2c-tools
libaio
libcrypto3
libgcc
libiio
libiio-tools
libintl
libserialport
libssl3
libstdc++
libusb
libxml2
mosquitto-libs
mosquitto-libs++
musl
musl-utils
picocom
scanelf
ssl_client
xz-libs
zlib
```
