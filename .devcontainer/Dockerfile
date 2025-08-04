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
    && rm -rf /var/lib/apt/lists/*

RUN curl -o- https://raw.githubusercontent.com/nvm-sh/nvm/v0.40.3/install.sh | bash
RUN export NVM_DIR="$HOME/.nvm" && \
    [ -s "$NVM_DIR/nvm.sh" ] && \. "$NVM_DIR/nvm.sh" && \
    [ -s "$NVM_DIR/bash_completion" ] && \. "$NVM_DIR/bash_completion"
RUN nvm install 22 && nvm use 22
RUN npm install -g @anthropic-ai/claude-code

RUN wget https://cosmo.zip/pub/cosmocc/cosmocc-4.0.2.zip && \
    unzip cosmocc-4.0.2.zip -d /opt/cosmos && \
    rm cosmocc-4.0.2.zip

ENV PATH="/opt/cosmos/bin:$PATH"

ENV CC="cosmocc -I/opt/cosmos/include -L/opt/cosmos/lib"
ENV CXX="cosmoc++ -I/opt/cosmos/include -L/opt/cosmos/lib"
ENV PKG_CONFIG="pkg-config --with-path=/opt/cosmos/lib/pkgconfig"
ENV INSTALL="cosmoinstall"
ENV AR="cosmoar"
