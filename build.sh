#!/usr/bin/env bash
set -e

echo "=========================================================="
echo "🚀 Building AnnieXBoda C++20 Music Bot (TDLib + NTgCalls)"
echo "=========================================================="

JOBS=$(nproc 2>/dev/null || echo 4)

# 1. Build TDLib if not already installed in /usr/local/lib
if [ ! -f "/usr/local/lib/libtdclient.a" ] && [ -d "third_party/td" ]; then
    echo "📦 [1/3] Compiling TDLib from third_party/td..."
    mkdir -p third_party/td/build
    cd third_party/td/build
    cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr/local ..
    cmake --build . --target install -j"$JOBS"
    cd ../../..
else
    echo "✅ [1/3] TDLib ready."
fi

# 2. Build Native C++ NTgCalls (NO PyTgCalls)
if [ ! -f "third_party/ntgcalls/static-output/lib/libntgcalls-native.a" ] && [ -d "third_party/ntgcalls" ]; then
    echo "📦 [2/3] Compiling Native C++ NTgCalls v3.0.3..."
    mkdir -p third_party/ntgcalls/build
    cd third_party/ntgcalls/build
    cmake -DCMAKE_BUILD_TYPE=Release -DSTATIC_BUILD=ON -DBINDING=c ..
    cmake --build . --target ntgcalls-native -j"$JOBS"
    cd ../../..
else
    echo "✅ [2/3] Native C++ NTgCalls ready."
fi

# 3. Build AnnieXBoda C++ Bot
echo "⚙️ [3/3] Compiling AnnieXBoda C++20 Bot executable..."
mkdir -p build
cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
cmake --build . -j"$JOBS"
cd ..

echo "🎉 Build finished successfully! Binary: ./build/annie_music_bot"
