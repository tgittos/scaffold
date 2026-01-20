FROM debian:bookworm-slim

RUN apt-get update && apt-get install -y \
    curl \
    git \
    wget \
    unzip \
    autotools-dev \
    autoconf \
    automake \
    m4 \
    valgrind \
    ripgrep \
    procps \
    libatomic1 \
    vim \
    zip \
    && rm -rf /var/lib/apt/lists/*

RUN touch ~/.bashrc && chmod +x ~/.bashrc
RUN curl -o- https://raw.githubusercontent.com/nvm-sh/nvm/v0.40.3/install.sh | bash
RUN . ~/.nvm/nvm.sh && . ~/.bashrc && nvm install node && npm i -g @anthropic-ai/claude-code

RUN wget https://cosmo.zip/pub/cosmocc/cosmocc-4.0.2.zip && \
    unzip cosmocc-4.0.2.zip -d /opt/cosmos && \
    rm cosmocc-4.0.2.zip

ENV PATH="/opt/cosmos/bin:$PATH"

ENV CC="cosmocc -I/opt/cosmos/include -L/opt/cosmos/lib"
ENV CXX="cosmoc++ -I/opt/cosmos/include -L/opt/cosmos/lib"
ENV PKG_CONFIG="pkg-config --with-path=/opt/cosmos/lib/pkgconfig"
ENV INSTALL="cosmoinstall"
ENV AR="cosmoar"
