
FROM fedora:42

RUN dnf install -y \
    cmake \
    ninja-build \
    clang \
    llvm-devel \
    && dnf clean all

WORKDIR /app
COPY . /app
RUN cmake --preset release
RUN cmake --build build
ENTRYPOINT ["build/kaleidoscope"]