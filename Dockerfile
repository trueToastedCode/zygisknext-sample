FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive

# Grundlegende Tools installieren
RUN apt-get update && apt-get install -y \
    curl \
    git \
    sudo \
    bash \
    vim \
    wget \
    unzip \
    build-essential \
    clang \
    openjdk-21-jdk \
    zip \
    lib32stdc++6 \
    lib32z1 \
    jq \
    cmake \
    ninja-build \
    && apt-get clean

# install Gradle
ARG GRADLE_VERSION=8.7
RUN wget https://services.gradle.org/distributions/gradle-${GRADLE_VERSION}-bin.zip && \
    unzip gradle-${GRADLE_VERSION}-bin.zip -d /opt && \
    rm gradle-${GRADLE_VERSION}-bin.zip && \
    GRADLE_HOME=/opt/gradle-${GRADLE_VERSION} && \
    echo "export GRADLE_HOME=$GRADLE_HOME" >> /etc/profile.d/env.sh && \
    echo "export PATH=\${GRADLE_HOME}/bin:\$PATH" >> /etc/profile.d/env.sh && \
    chmod +x /etc/profile.d/env.sh

# install Android NDK
ARG NDK_MAJOR_VERSION=25
RUN mkdir -p /tmp/ndk && \
    curl -s https://dl.google.com/android/repository/repository2-1.xml > /tmp/ndk/repo.xml && \
    NDK_LATEST=$(grep -o "android-ndk-r${NDK_MAJOR_VERSION}[^\"]*-linux.zip" /tmp/ndk/repo.xml | head -1) && \
    if [ -z "$NDK_LATEST" ]; then NDK_LATEST="android-ndk-r${NDK_MAJOR_VERSION}c-linux.zip"; fi && \
    wget https://dl.google.com/android/repository/${NDK_LATEST} -O /tmp/ndk.zip && \
    NDK_DIR=$(echo ${NDK_LATEST} | sed 's/-linux.zip//') && \
    ANDROID_NDK_HOME=/opt/${NDK_DIR} && \
    sed -i "\$i export ANDROID_NDK_HOME=${ANDROID_NDK_HOME}" /etc/profile.d/env.sh && \
    sed -i "\$ s|:\\\$PATH|:\${ANDROID_NDK_HOME}:\\\$PATH|" /etc/profile.d/env.sh && \
    unzip /tmp/ndk.zip -d /opt && \
    rm /tmp/ndk.zip && \
    rm -rf /tmp/ndk

# install Android SDK
RUN ANDROID_SDK_ROOT=/opt/android-sdk && \
    mkdir -p ${ANDROID_SDK_ROOT} && \
    cd ${ANDROID_SDK_ROOT} && \
    TOOLS_URL=$(curl -s https://developer.android.com/studio | grep -o 'https://dl.google.com/android/repository/commandlinetools-linux-[0-9]*_latest.zip' | head -1) && \
    if [ -z "$TOOLS_URL" ]; then TOOLS_URL="https://dl.google.com/android/repository/commandlinetools-linux-11076708_latest.zip"; fi && \
    wget ${TOOLS_URL} -O tools.zip && \
    mkdir -p cmdline-tools && \
    unzip tools.zip -d cmdline-tools && \
    mv cmdline-tools/cmdline-tools cmdline-tools/latest && \
    sed -i "\$i export ANDROID_SDK_ROOT=${ANDROID_SDK_ROOT}" /etc/profile.d/env.sh && \
    sed -i "\$ s|:\\\$PATH|:\${ANDROID_SDK_ROOT}/cmdline-tools/latest/bin:\\\$PATH|" /etc/profile.d/env.sh && \
    rm tools.zip

# install Android SDK components
ARG ANDROID_PLATFORM_VERSION=33
ARG ANDROID_NDK_VERSION=25
RUN . /etc/profile.d/env.sh && \
    yes | sdkmanager --sdk_root=${ANDROID_SDK_ROOT} --licenses && \
    sdkmanager --sdk_root=${ANDROID_SDK_ROOT} "platform-tools" && \
    LATEST_PLATFORM=$(sdkmanager --sdk_root=${ANDROID_SDK_ROOT} --list | grep "platforms;android-${ANDROID_PLATFORM_VERSION}" | head -1 | awk '{print $1}') && \
    if [ -z "$LATEST_PLATFORM" ]; then LATEST_PLATFORM="platforms;android-${ANDROID_PLATFORM_VERSION}"; fi && \
    LATEST_BUILD_TOOLS=$(sdkmanager --sdk_root=${ANDROID_SDK_ROOT} --list | grep "build-tools;${ANDROID_PLATFORM_VERSION}" | head -1 | awk '{print $1}') && \
    if [ -z "$LATEST_BUILD_TOOLS" ]; then LATEST_BUILD_TOOLS="build-tools;${ANDROID_PLATFORM_VERSION}.0.2"; fi && \
    LATEST_NDK=$(sdkmanager --sdk_root=${ANDROID_SDK_ROOT} --list | grep "ndk;${ANDROID_NDK_VERSION}" | head -1 | awk '{print $1}') && \
    if [ -z "$LATEST_NDK" ]; then LATEST_NDK="ndk;${ANDROID_NDK_VERSION}.2.9519653"; fi && \
    sdkmanager --sdk_root=${ANDROID_SDK_ROOT} \
        "${LATEST_PLATFORM}" \
        "${LATEST_BUILD_TOOLS}" \
        "${LATEST_NDK}" && \
    sed -i "\$ s|:\\\$PATH|:\${ANDROID_SDK_ROOT}/platform-tools:\\\$PATH|" /etc/profile.d/env.sh

# vscode user anlegen
RUN useradd -m vscode && \
    echo "vscode ALL=(ALL) NOPASSWD:ALL" >> /etc/sudoers

# copy entrypoint and set permission
COPY entrypoint.sh /entrypoint.sh
RUN chmod +x /entrypoint.sh

# set permission for ANDROID_SDK_ROOT
RUN . /etc/profile.d/env.sh && chmod -R a+w "${ANDROID_SDK_ROOT}"

# set user and workdir
USER vscode
WORKDIR /home/vscode

# set entrypoint
ENTRYPOINT ["/entrypoint.sh"]
