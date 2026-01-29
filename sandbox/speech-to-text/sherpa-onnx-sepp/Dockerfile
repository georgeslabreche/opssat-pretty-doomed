# Sherpa-ONNX speech-to-text for OPS-SAT SEPP
#
# Build from source for Alpine ARM32 (musl) compatibility
#
# PREREQUISITES:
#   1. Setup QEMU emulation:
#      docker run --rm --privileged tonistiigi/binfmt --install arm
#
#   2. Import exp_env:
#      docker import --platform linux/arm/v7 resources/exp_env.tar.gz exp_env:latest

FROM exp_env:latest

# Install build dependencies
RUN apk update && apk add --no-cache \
    build-base \
    cmake \
    git \
    libsndfile-dev

WORKDIR /app

# Create glibc compatibility stubs for musl
# The pre-built onnxruntime library requires glibc symbols not in musl
RUN printf '%s\n' \
    '#include <sys/stat.h>' \
    'char __libc_single_threaded = 0;' \
    'int backtrace(void **buffer, int size) { (void)buffer; (void)size; return 0; }' \
    'char **backtrace_symbols(void *const *buffer, int size) { (void)buffer; (void)size; return (char **)0; }' \
    'void backtrace_symbols_fd(void *const *buffer, int size, int fd) { (void)buffer; (void)size; (void)fd; }' \
    'int stat64(const char *path, struct stat *buf) { return stat(path, buf); }' \
    'int fstat64(int fd, struct stat *buf) { return fstat(fd, buf); }' \
    'int lstat64(const char *path, struct stat *buf) { return lstat(path, buf); }' \
    > /tmp/glibc_compat.c && \
    gcc -c /tmp/glibc_compat.c -o /usr/lib/glibc_compat.o && \
    rm /tmp/glibc_compat.c

CMD ["/bin/sh"]
