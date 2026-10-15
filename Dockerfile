# ── 𝚂ᴏᴜʀᴄᴇ ✘ 𝐁ᴏᴅᴀ © 2026 (C++20 Native TDLib & NTgCalls Dockerfile) ──────────
FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive \
    TZ=Africa/Cairo

WORKDIR /app

# 1. تثبيت أدوات بناء C++20 ومكتبات النظام و FFmpeg و yt-dlp
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential clang libc++-dev libc++abi-dev lld \
    cmake ninja-build gperf pkg-config git curl wget unzip ca-certificates \
    libssl-dev zlib1g-dev libreadline-dev libsqlite3-dev \
    libglib2.0-dev libpulse-dev libasound2-dev libx11-dev \
    ffmpeg aria2 python3 && \
    curl -L https://github.com/yt-dlp/yt-dlp/releases/latest/download/yt-dlp -o /usr/local/bin/yt-dlp && \
    chmod a+rx /usr/local/bin/yt-dlp && \
    apt-get clean && rm -rf /var/lib/apt/lists/*

# 2. نسخ ملفات السورس والمكتبات المرفقة (third_party/td و third_party/ntgcalls)
COPY . .

# 3. بناء مكتبة TDLib و C++ NTgCalls ثم ترجمة البوت بالكامل إلى ملف تنفيذي سريع
RUN chmod +x build.sh start setup && ./build.sh

# 4. تشغيل البوت مباشرة بدون أي وسيط بايثون أو pytgcalls
CMD ["./start"]
