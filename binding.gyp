{
  "targets": [
    {
      "target_name": "native_versions",
      "sources": ["native/addon.cc"],
      "defines": ["NAPI_VERSION=8"],
      "libraries": [
        "-Wl,--no-as-needed",
        "-lcrypto",
        "-lz",
        "-Wl,--as-needed",
        "-ldl"
      ],
      "cflags_cc": ["-std=c++17", "-Wall", "-Wextra", "-Wpedantic"]
    }
  ]
}
