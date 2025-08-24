
FROM fedora:42

RUN dnf install -y \
    cmake \
    ninja-build \
    clang \
    llvm-devel \
    && dnf clean all

WORKDIR /app
