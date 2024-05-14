FROM mcr.microsoft.com/devcontainers/cpp:1-debian-11

ARG REINSTALL_CMAKE_VERSION_FROM_SOURCE="3.22.2"

# Optionally install the cmake for vcpkg
COPY ./reinstall-cmake.sh /tmp/

# 
RUN if [ "${REINSTALL_CMAKE_VERSION_FROM_SOURCE}" != "none" ]; then \
        chmod +x /tmp/reinstall-cmake.sh && /tmp/reinstall-cmake.sh ${REINSTALL_CMAKE_VERSION_FROM_SOURCE}; \
    fi \
    && rm -f /tmp/reinstall-cmake.sh

RUN wget -O- https://apt.llvm.org/llvm-snapshot.gpg.key | gpg --dearmor | sudo tee /usr/share/keyrings/llvm.gpg \
    && echo """deb [signed-by=/usr/share/keyrings/llvm.gpg] http://apt.llvm.org/bullseye/ llvm-toolchain-bullseye-16 main \
    deb-src http://apt.llvm.org/bullseye/ llvm-toolchain-bullseye-16 main""" >> /etc/apt/sources.list
 
RUN apt-get update && export DEBIAN_FRONTEND=noninteractive \
    && apt-get -y remove llvm-11 clang-11 \
    && apt-get -y install --no-install-recommends clang-16 libclang-16-dev llvm-16-dev graphviz \
    && apt-get -y autoremove \
    && rm -rf /var/lib/apt/lists/*