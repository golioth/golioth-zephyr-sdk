FROM zephyrprojectrtos/zephyr-build:v0.19.0

USER root

### Gitpod user ###
# https://community.gitpod.io/t/how-to-resolve-password-issue-in-sudo-mode-for-custom-image/2395
#
# '-l': see https://docs.docker.com/develop/develop-images/dockerfile_best-practices/#user
RUN useradd -l -u 33333 -G sudo -md /home/gitpod -s /bin/bash -p gitpod gitpod \
    # passwordless sudo for users in the 'sudo' group
    && sed -i.bkp -e 's/%sudo\s\+ALL=(ALL\(:ALL\)\?)\s\+ALL/%sudo ALL=NOPASSWD:ALL/g' /etc/sudoers

RUN apt-get update && apt-get install -y libncurses5


USER gitpod
# Zephyr SDK
ENV ZEPHYR_SDK_INSTALL_DIR=/opt/toolchains/zephyr-sdk-0.13.1
ENV PATH="/opt/toolchains/gcc-arm-none-eabi-10-2020-q4-major/bin:${PATH}"
ENV PATH="/opt/toolchains/zephyr-sdk-0.13.1/x86_64-zephyr-elf/bin/:${PATH}"
RUN west config --global zephyr.base-prefer configfile
USER root