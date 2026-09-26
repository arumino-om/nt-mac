# NT mac
macOS上でWindowsアプリを動かす無謀なプロジェクト

## ビルド

必要なもの: CMake 3.20 以降，Xcode のコマンドラインツール (clang)，mingw-w64 (`brew install mingw-w64`)

```sh
cmake -S . -B build -G Ninja   # Ninja が無ければ -G "Unix Makefiles"
cmake --build build
(cd build && ctest)
./build/loader path/to/program.exe
```

## ディレクトリ構成

```
src/
├── dlls/          PE 側の DLL (mingw でビルド)．DLL 名ごとのディレクトリ
├── host/          ホスト側 (clang でビルド → loader)
│   ├── loader/    PE の解析・配置・モジュール管理
│   ├── runtime/   gs の切り替え・TEB/PEB・シグナル
│   └── ntmachost/ NTMACHOST.DLL (PE 側からホストへの唯一の入口) の実装
├── includes/
│   └── shared/    ホストと PE 側の両方で使う C ヘッダ
└── tests/
```

`dlls/` のコードは libc を呼ばず，ホストの機能は ntdll から NTMACHOST.DLL 経由でのみ使います．
`host/` のコードは PE から直接呼ばれることはなく，必ず gs を切り替えるスタブを経由します．
