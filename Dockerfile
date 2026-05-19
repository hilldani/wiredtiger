FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && \
    apt-get install -y --no-install-recommends \
    aspell=0.60.8.1-1build1 \
    aspell-en=2020.12.07-0-1 \
    ca-certificates=20240203 \
    ccache=4.9.1-1 \
    clang=1:18.0-59~exp2 \
    clang-format=1:18.0-59~exp2 \
    clang-tidy=1:18.0-59~exp2 \
    cmake=3.28.3-1build7 \
    curl=8.5.0-2ubuntu10.9 \
    diffutils=1:3.10-1build1 \
    doxygen=1.9.8+ds-2build5 \
    ed=1.20.1-1 \
    findutils=4.9.0-5build1 \
    g++=4:13.2.0-7ubuntu1 \
    gcc=4:13.2.0-7ubuntu1 \
    gdb=15.1-1ubuntu1~24.04.1 \
    git=1:2.43.0-1ubuntu7.3 \
    gzip=1.12-1ubuntu3.1 \
    liblz4-dev=1.9.4-1build1.1 \
    libsnappy-dev=1.1.10-1build1 \
    libsodium-dev=1.0.18-1ubuntu0.24.04.1 \
    libzstd-dev=1.5.5+dfsg2-2build1.1 \
    lld=1:18.0-59~exp2 \
    lldb=1:18.0-59~exp2 \
    llvm=1:18.0-59~exp2 \
    make=4.3-4.1build2 \
    ninja-build=1.11.1-2 \
    perl=5.38.2-3.2ubuntu0.2 \
    python3=3.12.3-0ubuntu2.1 \
    python3-dev=3.12.3-0ubuntu2.1 \
    python3-pip=24.0+dfsg-1ubuntu1.3 \
    python3-venv=3.12.3-0ubuntu2.1 \
    swig=4.2.0-2ubuntu1 \
    tar=1.35+dfsg-3build1 \
    universal-ctags=5.9.20210829.0-1 \
    zlib1g-dev=1:1.3.dfsg-3.1ubuntu2.1 \
    && rm -rf /var/lib/apt/lists/*

RUN useradd -m wiredtiger
USER wiredtiger

RUN python3 -m venv /home/wiredtiger/.venv
ENV PATH="/home/wiredtiger/.venv/bin:$PATH"

RUN pip install --no-cache-dir \
    find_libpython==0.4.0 \
    gcovr==5.0 \
    psutil==5.9.4 \
    ruff==0.4.5

WORKDIR /home/wiredtiger
