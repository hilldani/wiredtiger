FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    # Compilers, debuggers, static analysis
    # Note: clang provides both clang and clang++ binaries; llvm-dev omitted (version conflicts)
    gcc g++ clang lld llvm \
    clang-format clang-tidy \
    gdb lldb \
    # Build tools
    cmake ninja-build make swig ccache \
    # Python
    python3 python3-dev python3-pip python3-venv \
    # Compression & crypto libraries (auto-enabled by cmake when present)
    liblz4-dev libsnappy-dev zlib1g-dev libzstd-dev libsodium-dev \
    # Tools required by dist/s_all and related scripts
    ed perl aspell aspell-en universal-ctags doxygen \
    # Standard Unix tools (some may already be in the base image, listed for clarity)
    git curl ca-certificates tar gzip findutils diffutils \
    && rm -rf /var/lib/apt/lists/*

# Python packages required by the test suite and dist/ tooling.
# ruff version must match dist/ruff.toml exactly or dist/s_all fails.
RUN pip3 install --break-system-packages \
    ruff==0.4.5 \
    psutil==5.9.4 \
    gcovr==5.0 \
    find_libpython==0.4.0

# Evergreen CLI — install manually after building this image.
# The download endpoint requires MongoDB SSO authentication and cannot be fetched
# during an unauthenticated build. Once logged in via a browser, download from:
#   https://evergreen.mongodb.com/clients/linux_amd64/evergreen  (x86-64)
#   https://evergreen.mongodb.com/clients/linux_arm64/evergreen  (arm64)
# Then copy it in: docker cp evergreen <container>:/usr/local/bin/evergreen

WORKDIR /wiredtiger
