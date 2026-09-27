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
│   ├── runtime/   gs の切り替え・TEB/PEB・シグナル・PE のスレッド
│   ├── win32k/    ウィンドウ・メッセージ・描画 (Windows の win32k 相当) と Cocoa との橋渡し
│   └── ntmachost/ NTMACHOST.DLL (PE 側からホストへの唯一の入口) の実装
├── includes/
│   └── shared/    ホストと PE 側の両方で使う C ヘッダ
└── tests/
```

## デバッグ用の環境変数

| 変数 | 内容 |
|---|---|
| `NTMAC_TRACE=loaddll,unresolved,relay` | DLL の読み込み / スタブにしたインポート / DLL の境界を越える呼び出し (引数と戻り値) を表示する |
| `NTMAC_RELAY_EXCLUDE=ntdll,kernel32.HeapAlloc` | relay の表示から除く DLL や関数 |
| `NTMAC_UNIMPLEMENTED=abort` (既定) / `return0` / `strict` | 未実装の関数が呼ばれたときの動作 (終了 / 0 を返して続行 / 読み込み時にエラー) |
| `NTMAC_DLL_PATH=dir1:dir2` | DLL を探すディレクトリを追加する |
| `NTMAC_NO_DISPLAY=1` | 画面を使わずに動かす (ウィンドウはホストの中にだけ存在する．テスト用) |
| `NTMAC_DUMP_DIR=dir` | ウィンドウを描画するたびに，その内容を `dir/window-<HWND>.png` に書き出す |

MSVC でビルドしたプログラムを動かすには，Microsoft の native の DLL (`msvcp140.dll`，`vcruntime140.dll`，`vcruntime140_1.dll`，`ucrtbase.dll` など) を exe と同じディレクトリに置きます．

`dlls/` のコードは libc を呼ばず，ホストの機能は ntdll から NTMACHOST.DLL 経由でのみ使います．
`host/` のコードは PE から直接呼ばれることはなく，必ず gs を切り替えるスタブを経由します．
