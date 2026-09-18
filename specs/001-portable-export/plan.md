---
feature: portable-export
test: pwsh -NoProfile -File scripts/test-core.ps1
---

# 実装計画 — 素材ごとのポータブル書き出し(Windows)

> plan.md — 「どう作るか」。spec.md の受け入れ条件を満たす設計。

## アプローチ

2 層に分ける。

1. **`core/` — 変換と書き出しの本体。** C++17 の静的ライブラリ `portable_core`。**OBS にも Qt にも依存しない**。
   JSON の書き換え(`plan_conversion`)は純粋関数にしてファイルの有無の判定を差し替え可能にし、
   ファイルのコピー・JSON 出力・zip 作成(`export_collection` / `write_zip`)は実ファイルシステムでテストする。
   テストは doctest。ローカルでは WSL(g++ 13 + `uvx` の CMake/Ninja)で、CI では Ubuntu と Windows(MSVC)で回す。
2. **`src/` — OBS との接続。** obs-plugintemplate をベースにしたプラグイン本体。ツールメニュー → Qt ダイアログ →
   別スレッドで `export_collection` → 結果ダイアログ。OBS SDK が要るのでローカルではビルドせず、
   GitHub Actions(template の `windows-build`)でビルドが通ることを合否にする。動作は人が実機で確かめる(H1)。

分け方の理由: この PC には MSVC も OBS の開発用一式も無く、プラグイン本体をローカルでビルド・テストできない。
壊れやすいのは変換規則(名前付け・衝突・欠落・日本語)なので、そこを OBS から切り離してヘッドレスで全部テストし、
OBS 依存部分は「呼んで結果を出すだけ」の薄さに抑える。

### 変換規則(`plan_conversion`)

- **走査の仕方**: JSON 全体を深さ優先・出現順にたどる(キーの順は `ordered_json` の順)。**「設定オブジェクト」**=
  キー `settings` またはキー `transition` の値であるオブジェクト。設定オブジェクトを見つけたら、その**直下のキー**に下の 1・2 を当てる。
  設定オブジェクトの中もさらにたどる(シーンの `settings.items[]` → `show_transition` / `hide_transition` → `transition` のように入れ子になるため)。
  これで次の全部に届く(いずれも OBS 31 のソースで保存形を確認済み):
  トップレベル `sources[]` / `groups[]` / `transitions[]` の `settings`、各ソースの `filters[].settings`、
  シーンアイテムの表示/非表示トランジション `settings.items[].show_transition.transition` / `hide_transition.transition`、
  `canvases` 以下に同じ形があればそれも。グループ内のソースはトップレベルの `sources` に入っている。
- **ソース名**: その設定オブジェクトを持っているオブジェクト(`settings` / `transition` キーの親)の `name`。文字列でなければ `""`。
- **設定オブジェクト直下の処理**:
  1. キー `file` → `local_file` → `path` → `image_path` → `track_matte_path` → `text_file` の順に、値が文字列かつ `is_local_path` が真なら「単体参照」として処理
     (それぞれ 画像・GDI+ テキスト / メディア・ブラウザ / スティンガー / 画像マスク・LUT フィルタ / スティンガーのトラックマット / FreeType テキスト。
     OBS の各プラグインが `missing_files` で見ているキーと同じ)
  2. キー `files` → `playlist` の順に、値が配列なら、そのオブジェクト要素の `value` が文字列かつ `is_local_path` が真のものを「配列参照」として処理。
     `value` 以外のキー(`hidden` `selected` 等)は触らない
- **トップレベルの `modules` は丸ごと走査しない**(スクリプトなどのプラグインが `settings` を持つことがあり、素材ではないパスを拾うため)。設定オブジェクトの外にある文字列も、キー名が同じでも触らない。
- **`is_local_path(v)`**: 次のどれかなら真。① `^[A-Za-z]:[\\/]`(ドライブ付き) ② `/` で始まる ③ `\` で始まる。
  それ以外(空文字、`./` `../` で始まる相対パス、`http://` `https://` `file://` 等)は偽 = 触らない・欠落扱いもしない。
- **ファイルの状態**: `StatFn` で `Missing` / `RegularFile` / `Directory` を得る。`RegularFile` 以外は値を変えずに
  `missing` へ積む(理由 `NotFound` / `IsDirectory`)。`copies` には積まない。
- **出力名**:
  - 単体参照: `ext` = 元の値の最後のパス要素(`/` と `\` の両方を区切りとみなす)の拡張子(最後の `.` 以降、`.` を含む。
    要素が `.` で始まり他に `.` が無いなら拡張子なし)。`stem_raw` = ソース名。ただしソース名が `ext` で終わる(ASCII 大小無視)なら
    その分を取り除く(`bg.png` という名前のソースが `bg.png.png` にならない)
  - 配列参照: 最後のパス要素を `stem_raw` と `ext` に分ける(元のファイル名を使う)
  - `make_asset_name(stem_raw, ext)` = `sanitize_filename(stem_raw)` + `ext`
- **`sanitize_filename(s)`**: ① `< > : " / \ | ? *` と 0x00–0x1F を `_` に置換 ② 前後の ASCII 空白を削る ③ 末尾の `.` を削る
  ④ 空なら `asset` ⑤ UTF-8 で 80 バイトを超えたら、文字の途中で切らない位置で 80 バイト以内に切る
  ⑥ 大小無視で `CON` `PRN` `AUX` `NUL` `COM1`–`COM9` `LPT1`–`LPT9` に一致したら末尾に `_` を足す
- **同じファイルの判定 `same_file_key(p)`**: **文字列操作だけで作る(`std::filesystem::path` を通さない)。**
  ① `\` を `/` に置換 ② 先頭の `//`(UNC)は保ったまま、残りを `/` で分割し、空要素と `.` を捨て、`..` は直前の要素を 1 つ消す(消す要素が無ければ捨てる)
  ③ `/` で連結し直す ④ ASCII の `A`–`Z` だけ小文字化(UTF-8 の他のバイトは触らない)。**中身のハッシュでは判定しない**。
- **衝突の解き方**: `key → 出力名` と `出力名(ASCII 小文字) → key` の 2 つの表を持つ。
  1. その key に出力名が既にあれば、それを使う(ソース名が違っても 1 コピーを共有)
  2. 候補名 `stem + ext` が未使用ならそれを使う
  3. 使用済みなら `stem_2 + ext`、`stem_3 + ext` … と最初の未使用名を使う
- **書き換え後の値**: `./assets/<出力名>`。`copies` には初出の key だけを出現順に積む。
- JSON のそれ以外の部分(`name` を含む)は変えない。キーの順番も保つ(`nlohmann::ordered_json`)。

### 書き出し(`export_collection`)

1. `base = sanitize_filename(collection["name"])`(`name` が文字列でなければ `scene-collection`)
2. `out_dir = next_free_dir(parent_dir, base)`: `parent/base` のフォルダも `parent/base.zip` も無ければ `base`、
   あれば `base_2` `base_3` … と両方とも無い最初の名前。**既存のフォルダ・ファイルは消さない・上書きしない**
3. `plan_conversion(collection, stat_file)` を呼ぶ
4. `out_dir/assets/` を作り、`copies` を順にコピー(`std::filesystem::copy_file`)。1 件ごとに `progress(done, total)`
5. `out_dir/<base>.json` に `converted.dump(2)` + 改行を UTF-8(BOM 無し)で書く
6. `make_zip` なら `write_zip(parent/<out_dir のフォルダ名>.zip, out_dir)`
7. コピー・書き込みに失敗したら `ExportError`(`std::runtime_error` 派生、メッセージに対象パス)を投げる。途中までの出力は消さない

**UTF-8 とパスの変換は必ず `path_from_utf8` / `utf8_from_path` を通す。** `std::filesystem::path(std::string)` は
Windows で ANSI コードページとして解釈され、日本語のパスが壊れる。C++17 なので `path_from_utf8(s)` = `std::filesystem::u8path(s)`、
`utf8_from_path(p)` = `p.u8string()`(C++17 では `std::string` を返す)。2 つは `convert.hpp` / `convert.cpp` に置き、
`stat_file` もこれで開く(`std::filesystem::status` に `path_from_utf8` の結果を渡す)。
**言語規格は C++17 に固定**(template の `CMAKE_CXX_STANDARD 17` と同じ。C++20 にすると `u8path` / `u8string` の型が変わる)。

### zip(`write_zip`)

- `root_dir` 配下の全ファイルを、`root_dir` からの相対パス(区切りは `/`)で格納する。zip の直下に `<base>.json` と `assets/` が来る
- 無圧縮(STORED)。素材は画像・動画で既に圧縮済みのため
- ファイル名は UTF-8 で書き、汎用フラグの bit 11(UTF-8)を立てる
- **miniz の `fopen` を使う API(`mz_zip_writer_init_file*` / `mz_zip_writer_add_file`)は使わない。** Windows で日本語パスを
  開けないため。書き込みは `mz_zip_writer_init_v2` + `m_pWrite`(`std::ofstream` に書くコールバック)、読み込みは
  `mz_zip_writer_add_read_buf_callback` + `std::ifstream`(どちらも `path_from_utf8` で開く)。4 GiB を超えても書けるよう zip64 を許す

### プラグイン(`src/`)

- メニュー: `obs_module_load` で `obs_frontend_add_tools_menu_item(obs_module_text("PortableExport.Menu"), on_export_menu, nullptr)`。
  `on_export_menu` は `run_export(static_cast<QWidget *>(obs_frontend_get_main_window()))` を呼ぶ
- `run_export(parent)` の流れ(全部 UI スレッド):
  0. 書き出し中(下の `g_job` が生きている)なら `PortableExport.Busy` を `QMessageBox::information` で出して戻る
  1. `ExportDialog`(QDialog)を開く — 出力先の親フォルダ(`QLineEdit` + 「参照…」= `QFileDialog::getExistingDirectory`)、
     `QCheckBox`「zip も作る」、OK / キャンセル。前回の親フォルダと zip の選択は `obs_frontend_get_user_config()` の
     セクション `PortableExport`、キー `LastDir` / `MakeZip` に保存して次回の初期値にする。親フォルダが空・存在しないなら OK を押せない
  2. OK で `obs_frontend_save()` を呼ぶ。**これは保存を `Qt::QueuedConnection` で予約するだけで、戻った時点ではまだ書いていない**
     (OBS 31 `OBSBasic::SaveProject` → `SaveProjectDeferred`)。そこで続き(3 以降)を
     `QMetaObject::invokeMethod(parent, [..]{...}, Qt::QueuedConnection)` で**後ろに積む**。同じ UI スレッドのキューで先に積まれた保存が先に走るので、
     3 で読むファイルは保存後のものになる
  3. `current_collection_file()` で JSON のパスを得て、`std::ifstream`(`path_from_utf8` で開く)で読み `portable::Json::parse`。
     パスが空・読めない・パース失敗は `QMessageBox::critical`(`PortableExport.Failed` + `PortableExport.NoCollection` / `PortableExport.ReadFailed`)で戻る
  4. `QProgressDialog`(`Qt::WindowModal`、キャンセルボタン無し、`setMinimumDuration(0)`)を出し、
     `g_job = std::make_unique<ExportJob>()` を作って `g_job->thread = std::thread(...)` で `export_collection` を走らせる。
     `progress` と完了・例外は `QMetaObject::invokeMethod(progress_dialog, ..., Qt::QueuedConnection)` で UI スレッドへ渡す
     (`progress_dialog` は `QPointer` で持ち、消えていたら何もしない)
  5. 完了を受けた UI スレッド側で `g_job->thread.join()` → `g_job.reset()` → 進捗ダイアログを閉じる → 結果を出す:
     `QMessageBox`(タイトル `PortableExport.Done`)の本文に `PortableExport.OutDir` / `PortableExport.Copied` / (zip を作ったとき)`PortableExport.ZipPath`、
     欠落があれば `PortableExport.Missing` を足し、`detailedText` に 1 行 1 件 `ソース名<TAB>元のパス<TAB>理由`
     (理由は `PortableExport.Reason.NotFound` / `PortableExport.Reason.IsDirectory`)。ボタン「`PortableExport.OpenFolder`」で
     `QDesktopServices::openUrl(QUrl::fromLocalFile(out_dir))`
  6. `ExportError` などの例外は 5 と同じく join・reset してから `QMessageBox::critical`(`PortableExport.Failed` + 例外メッセージ)
- `ExportJob` = `{ std::thread thread; }`。`g_job` は `src/export-dialog.cpp` のファイル内 `static std::unique_ptr<ExportJob>`。
  `obs_module_unload` から `export_shutdown()` を呼び、`g_job` があれば `thread.join()` してから破棄する(書き出し途中で OBS を閉じても落ちない。閉じるのは書き出しが終わるまで待つ)
- `current_collection_file()`:
  1. フォルダ: `config_get_string(obs_frontend_get_app_config(), "Locations", "SceneCollections")` を `path_from_utf8` し `/ "obs-studio/basic/scenes"`。
     値が空かフォルダが無ければ、`obs_module_config_path("")`(`…/obs-studio/plugin_config/obs-portable-export/`)の末尾の区切りを落として
     `parent_path()` を 2 回した先 `/ "basic/scenes"`(OBS 31 `OBSApp.cpp` の `userScenesLocation` と同じ決め方)
  2. ファイル名: `config_get_string(obs_frontend_get_user_config(), "Basic", "SceneCollectionFile")`。OBS 31 は `.json` 込みで保存しているので、
     末尾が `.json`(ASCII 大小無視)でなければ `.json` を足す
  3. そのファイルが無ければ、フォルダ内の `*.json` を読んで `"name"` が `obs_frontend_get_current_scene_collection()` と一致する最初のものを返す。
     それも無ければ空の path
- 表示文字列は `data/locale/en-US.ini` と `data/locale/ja-JP.ini` に置き、`obs_module_text` で引く

## 変更点

この repo は README と LICENSE しか無い。以下は全て新規(ベースは obs-plugintemplate `3e7d7ac3b5342cd7d9b88890b9c70b472d1520fc`)。

| 対象(ファイル / 関数) | 変更 |
|---|---|
| obs-plugintemplate 一式(`CMakeLists.txt` `CMakePresets.json` `buildspec.json` `cmake/` `build-aux/` `.github/` `.clang-format` `.gersemirc` `src/plugin-support.c.in` `src/plugin-support.h`) | 上記コミットからコピー。`LICENSE` と `README.md` はこちらのものを残す |
| `buildspec.json` | `name` = `obs-portable-export`、`displayName` = `Portable Export`、`version` = `0.1.0`、`author` = `max-enterme`、`website` = `https://github.com/max-enterme/obs-portable-export`、`email` = `112470175+max-enterme@users.noreply.github.com`、`platformConfig.macos.bundleId` = `com.max-enterme.obs-portable-export` |
| `CMakeLists.txt`(B3) | `ENABLE_FRONTEND_API` と `ENABLE_QT` の既定を `ON`。`add_subdirectory(core)` し `portable_core` をリンク。`target_sources` を `src/plugin-main.cpp` だけに差し替え |
| `CMakeLists.txt`(B4) | `target_sources` に `src/export-dialog.cpp` `src/collection-file.cpp` を足す |
| `src/plugin-main.c`(B3) | 削除し `src/plugin-main.cpp` に置き換え。B3 では `obs_module_load` でメニュー項目を足し、`on_export_menu` は `blog(LOG_INFO, "[obs-portable-export] menu clicked")` だけ。`obs_module_unload` は空 |
| `src/plugin-main.cpp`(B4) | `on_export_menu` を `run_export(...)` 呼び出しに、`obs_module_unload` を `export_shutdown()` 呼び出しに差し替え |
| `.github/workflows/build-project.yaml` | `macos-build` / `ubuntu-build` ジョブを削除(Windows だけ) |
| `.github/workflows/push.yaml` `.github/workflows/pr-pull.yaml` | `check-format` ジョブを削除(下の「採らない案」) |
| `.github/workflows/check-format.yaml` | 削除 |
| `.github/workflows/core-tests.yaml` | 新規。`push` と `pull_request` で、`ubuntu-24.04` と `windows-2022` の matrix で core をビルドして `ctest` |
| `core/CMakeLists.txt` | 新規。`portable_core` 静的ライブラリ。依存は FetchContent(`URL` + `URL_HASH SHA256`)で nlohmann/json 3.11.3(`json.tar.xz`)・miniz 3.0.2(リリース zip)・doctest 2.4.11(`PORTABLE_CORE_TESTS=ON` のときだけ)。単体で `cmake -S core` できる |
| `core/include/portable/convert.hpp` `core/src/convert.cpp` | 新規。変換規則(下の I/F) |
| `core/include/portable/export.hpp` `core/src/export.cpp` | 新規。書き出し(パス変換は convert.hpp にあるものを使い、ここで定義し直さない) |
| `core/include/portable/zip.hpp` `core/src/zip.cpp` | 新規。zip 作成 |
| `core/tests/test_main.cpp` `core/tests/test_convert.cpp` `core/tests/test_export.cpp` `core/tests/test_zip.cpp` | 新規。doctest(`test_main.cpp` は `DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN` だけ) |
| `scripts/test-core.ps1` | 新規。WSL で core をビルドしてテスト(下記) |
| `src/export-dialog.hpp` `src/export-dialog.cpp` | 新規。`ExportDialog` と実行・結果表示の関数 `run_export(QWidget *parent)` |
| `src/collection-file.hpp` `src/collection-file.cpp` | 新規。`current_collection_file()` |
| `data/locale/en-US.ini` `data/locale/ja-JP.ini` | 新規。表示文字列 |
| `.gitignore` | `build-core-test/` と template の `.gitignore` の行を足す |

`scripts/test-core.ps1` の中身:

```powershell
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$wslRoot = (wsl -d Ubuntu-24.04 -u max wslpath -a ($root -replace '\\', '/')).Trim()
wsl -d Ubuntu-24.04 -u max bash -lc "set -e; cd '$wslRoot'; uvx --from cmake --with ninja cmake -G Ninja -S core -B build-core-test -DCMAKE_BUILD_TYPE=Debug -DPORTABLE_CORE_TESTS=ON; uvx --from cmake --with ninja cmake --build build-core-test; uvx --from cmake ctest --test-dir build-core-test --output-on-failure"
exit $LASTEXITCODE
```

## 新規インターフェース

```cpp
// core/include/portable/convert.hpp
namespace portable {
using Json = nlohmann::ordered_json;

enum class FileKind { Missing, RegularFile, Directory };
using StatFn = std::function<FileKind(const std::string &utf8_path)>;

enum class MissingReason { NotFound, IsDirectory };
struct MissingFile {
	std::string source_name;   // 要素の "name"。無ければ ""
	std::string original_path; // 元の値そのまま
	MissingReason reason;
};
struct PlannedCopy {
	std::string src_path; // 元の値そのまま(UTF-8)
	std::string dst_name; // assets/ 内のファイル名(UTF-8)
};
struct ConversionPlan {
	Json converted;
	std::vector<PlannedCopy> copies;  // 初出順。dst_name は大小無視で重複なし
	std::vector<MissingFile> missing; // 出現順
};

bool is_local_path(const std::string &value);
std::string sanitize_filename(const std::string &name);
std::string same_file_key(const std::string &utf8_path);
ConversionPlan plan_conversion(const Json &collection, const StatFn &stat);
FileKind stat_file(const std::string &utf8_path); // 実ファイルシステム版。path_from_utf8 で開く

std::filesystem::path path_from_utf8(const std::string &utf8); // = std::filesystem::u8path(utf8)
std::string utf8_from_path(const std::filesystem::path &p);    // = p.u8string()
class ExportError : public std::runtime_error { using std::runtime_error::runtime_error; };
}

// core/include/portable/export.hpp
namespace portable {
struct ExportOptions {
	std::filesystem::path parent_dir;
	bool make_zip = false;
};
struct ExportResult {
	std::filesystem::path out_dir;
	std::filesystem::path json_path;
	std::filesystem::path zip_path; // make_zip == false なら空
	size_t copied = 0;
	std::vector<MissingFile> missing;
};
using ProgressFn = std::function<void(size_t done, size_t total)>;

std::filesystem::path next_free_dir(const std::filesystem::path &parent, const std::string &base);
ExportResult export_collection(const Json &collection, const ExportOptions &opt, const ProgressFn &progress = {});
}

// core/include/portable/zip.hpp
namespace portable {
void write_zip(const std::filesystem::path &zip_path, const std::filesystem::path &root_dir); // 失敗時 ExportError
}

// src/collection-file.hpp
std::filesystem::path current_collection_file(); // 見つからなければ空の path

// src/export-dialog.hpp
void run_export(QWidget *parent); // メニューから呼ぶ。ダイアログ〜結果表示まで
void export_shutdown();           // obs_module_unload から呼ぶ。書き出し中なら終わるまで待つ
```

`core/CMakeLists.txt` の依存: miniz のリリース zip は `miniz.c` / `miniz.h` だけの amalgamation なので、
`add_library(miniz STATIC ${miniz_SOURCE_DIR}/miniz.c)` を自前で書き、`target_include_directories(miniz SYSTEM PUBLIC ${miniz_SOURCE_DIR})`。
nlohmann/json は `json.tar.xz` を `FetchContent_MakeAvailable` し `nlohmann_json::nlohmann_json` をリンク。
`portable_core` は `target_compile_features(portable_core PUBLIC cxx_std_17)`。MSVC のときは `portable_core` とテストに `/utf-8` を付ける(`cmake -S core` 単体では template の設定が入らず、日本語の文字列リテラルが化けうるため)。`miniz` ターゲットは `COMPILE_WARNING_AS_ERROR OFF`(template は警告をエラーにするが、`SYSTEM` は miniz.c 自体のコンパイル警告を抑えないため)。`URL_HASH SHA256` の値は実装時に取得して書く。

表示文字列(`data/locale/ja-JP.ini` / `en-US.ini`):

| キー | ja-JP | en-US |
|---|---|---|
| `PortableExport.Menu` | ポータブル書き出し… | Portable Export… |
| `PortableExport.Title` | ポータブル書き出し | Portable Export |
| `PortableExport.ParentDir` | 出力先 | Output folder |
| `PortableExport.Browse` | 参照… | Browse… |
| `PortableExport.MakeZip` | zip も作る | Also create a zip |
| `PortableExport.Progress` | 素材をコピーしています… | Copying media files… |
| `PortableExport.Done` | 書き出しました | Export finished |
| `PortableExport.Missing` | 見つからなかった素材: %1 件(元のパスのまま残しています) | Missing files: %1 (original paths kept) |
| `PortableExport.OpenFolder` | フォルダを開く | Open folder |
| `PortableExport.Failed` | 書き出しに失敗しました | Export failed |
| `PortableExport.OutDir` | 出力フォルダ: %1 | Output folder: %1 |
| `PortableExport.Copied` | コピーした素材: %1 件 | Copied files: %1 |
| `PortableExport.ZipPath` | zip: %1 | Zip: %1 |
| `PortableExport.Reason.NotFound` | ファイルが無い | not found |
| `PortableExport.Reason.IsDirectory` | フォルダを指している | is a folder |
| `PortableExport.NoCollection` | 現在のシーンコレクションのファイルが見つかりません | Could not find the current scene collection file |
| `PortableExport.ReadFailed` | シーンコレクションのファイルを読めません: %1 | Could not read the scene collection file: %1 |
| `PortableExport.Busy` | 書き出し中です。終わるまでお待ちください | An export is already running |

`%1` は `QString::arg` で埋める(`obs_module_text` の結果を `QString::fromUtf8` してから)。

## 採らない案

- **Python などのスクリプトを同梱して変換させる** — 書き出す側に Python の導入を求めることになる。C++ ネイティブにする。
- **Scene Collection Manager に機能を足す(PR / fork)** — 同プラグインは Import 側とセットの設計で、`./` 無しの形式を変えると既存利用者の Import が壊れる。独立したプラグインにする。
- **元ファイルの探索を「ファイル名だけで別フォルダを探す」方式にする** — 手元の CLI は別 PC の NAS パスを救うためにこれを持つが、プラグインは素材がある PC 上で動くので絶対パスがそのまま使える。ファイル名検索は別フォルダの同名ファイルを 1 つに潰す副作用がある。
- **同じファイルでもソース名ごとに別コピーを作る** — 出力が太るだけで、受け取り側の見た目は変わらない。1 コピー共有にする。
- **同じファイルを中身のハッシュで判定する** — 数百 MB の動画を全部読むことになり遅い。パスの一致で足りる(別パスの同一内容が 2 コピーになるだけで壊れはしない)。
- **既存の出力フォルダを上書き・削除する** — 利用者の別のファイルを消す事故が起こりうる。`_2` で別名にする。
- **ライブラリを使わず自前で JSON を扱う / OBS の `obs_data` で扱う** — `obs_data` を使うと core が libobs に依存し、ヘッドレスでテストできない。nlohmann/json の `ordered_json` でキー順を保つ。
- **zip を圧縮あり(DEFLATE)で作る** — 素材の大半は既に圧縮済みの画像・動画で、縮まないのに時間だけかかる。
- **template の clang-format / gersemi チェックを残す** — ローカルに同じ版の clang-format が無く、main への push で毎回落ちる状態になる。整形は実装時に揃えるが CI の門にはしない。
- **`obs_frontend_save()` の直後に同期でファイルを読む** — 保存は予約されるだけなので、未保存の変更が抜けた古い内容を書き出す。続きを `Qt::QueuedConnection` で積む。
- **OBS のデータ(`obs_frontend_get_scenes` 等)から JSON を自前で組み立てる** — OBS の保存形式を複製することになり、版が上がるたびにずれる。OBS 自身が保存したファイルを読む。
- **トップレベルの `sources` / `groups` / `transitions` と `settings.items` だけを決め打ちで見る** — フィルタ(画像マスク・LUT)やシーンアイテムの表示/非表示トランジション(スティンガー)の素材が漏れ、受け取り側で不足ファイルになる。「設定オブジェクト」を全体から探す。
- **macOS / Linux も同時に出す** — 実機確認できる環境が無い。加えて OBS 標準インポートの `CheckPath` はルートフォルダ名と `./` をそのまま連結する(`C:/dir` + `./assets/x` → `C:/dir./assets/x`)ため、Windows では末尾の `.` が無視されて通るが、それ以外の OS で開けるかは未確認。

## テスト

`test`(`scripts/test-core.ps1`)で次が全部通れば OK。

- `is_local_path`: `C:/a.png` `C:\a.png` `/home/a.png` `\\nas\a.png` `//nas/a.png` が真。`""` `./assets/a.png` `../a.png` `https://example.com/a.png` `file:///C:/a.png` `ab` が偽
- `sanitize_filename`: `背景画像` → そのまま / `a<>:"/\|?*b` → `a_________b` / `"  name  "` → `name` / `name...` → `name` / `""` → `asset` / `con` → `con_` / 日本語 40 文字(120 バイト)→ 26 文字(78 バイト)で切れる
- `same_file_key`: `C:\Media\BG.png` と `c:/media/./bg.png` と `C:/Media/x/../BG.png` が同じ値。`C:/素材/背景.png` と `C:/素材/前景.png` は違う値。`//nas/a/../b.png` → `//nas/b.png`
- `path_from_utf8` / `utf8_from_path`: `素材/背景.png` を往復させて同じ文字列に戻る(Windows の CI で ANSI 解釈の事故を捕まえる)
- `plan_conversion`:
  - 単体参照 `file` / `local_file` / `path` / `image_path` / `track_matte_path` / `text_file` がそれぞれ `./assets/<ソース名><拡張子>` になる
  - `transitions[].settings`、`groups[].settings`、`sources[].filters[].settings`、`sources[].settings.items[].show_transition.transition` / `hide_transition.transition` の中も変わる。ソース名はそれぞれ持ち主の `name`
  - 設定オブジェクトの外、およびトップレベル `modules` の中(`settings` を含んでいても)は変わらない
  - ソース名 `bg.png` + `C:/m/bg.png` → `./assets/bg.png`
  - `url` キーと相対パス・URL の値は変わらない
  - スライドショー `files[].value` と VLC `playlist[].value` は元のファイル名で `./assets/<元の名前>` になり、`hidden` `selected` は残る
  - 別ファイルで同じ出力名 → `画像.png` `画像_2.png`。大小違い(`BG.png` と `bg.png` の別ファイル)も衝突として `_2`
  - 同じファイルを 2 つのソース(名前違い)が参照 → 両方同じ出力名、`copies` は 1 件
  - 見つからないファイル → 値はそのまま、`missing` に `NotFound` で 1 件。フォルダ → `IsDirectory`
  - `settings` が無い要素・`name` が無い要素・`sources` が無い JSON で落ちない
  - `name` やその他のキーは変わらず、キーの順番も元のまま
- `export_collection`(一時フォルダに実ファイルを置く):
  - `<親>/<名前>/<名前>.json` と `assets/` ができ、コピーはバイト一致、JSON を読み直すと値が `./assets/...`
  - 日本語のコレクション名・ソース名・元ファイル名で、出力のファイル名が期待どおり
  - 同名フォルダがあると `<名前>_2`。`<名前>/` と `<名前>_2.zip` があると `<名前>_3`(zip だけがある番号も避ける)
  - `progress` が `(1,N)` … `(N,N)` の順で N 回呼ばれる
  - 出力先に書けない(`parent_dir` がファイル)とき `ExportError`
- `write_zip`: 読み戻すと全ファイルがそろい中身が一致、名前の区切りが `/`、日本語名のエントリは bit 11 が立っている

GitHub Actions: `core-tests`(Ubuntu / Windows)と `build-project` の `windows-build` が緑。

## テストケース

| テスト名 | 置き場(ファイル) | 入力・前提 | 期待値 |
|---|---|---|---|
| `is_local_path: 絶対パスを判定する` | `core/tests/test_convert.cpp` | `C:/a.png` `C:\a.png` `/home/a.png` `\\nas\a.png` `//nas/a.png` | 全部 `true` |
| `is_local_path: 相対パスと URL は対象外` | `core/tests/test_convert.cpp` | `""` `./assets/a.png` `../a.png` `https://example.com/a.png` `file:///C:/a.png` `ab` | 全部 `false` |
| `sanitize_filename: 置換と削り` | `core/tests/test_convert.cpp` | `背景画像` / `a<>:"/\|?*b` / `"  name  "` / `name...` / `""` / `con` | `背景画像` / `a_________b` / `name` / `name` / `asset` / `con_` |
| `sanitize_filename: 80 バイトで切る` | `core/tests/test_convert.cpp` | `あ` × 40 | `あ` × 26(78 バイト) |
| `same_file_key: 表記ゆれを畳む` | `core/tests/test_convert.cpp` | `C:\Media\BG.png` / `c:/media/./bg.png` / `C:/Media/x/../BG.png` | 3 つとも `c:/media/bg.png` |
| `same_file_key: 日本語の別ファイルは別の値` | `core/tests/test_convert.cpp` | `C:/素材/背景.png` と `C:/素材/前景.png` | 違う文字列(`c:/素材/背景.png` と `c:/素材/前景.png`) |
| `same_file_key: UNC を保つ` | `core/tests/test_convert.cpp` | `\\nas\a\..\b.png` | `//nas/b.png` |
| `utf8 パス変換: 往復` | `core/tests/test_convert.cpp` | `utf8_from_path(path_from_utf8("素材/背景.png"))` | `"素材/背景.png"`(Windows の CI では区切りを `/` に揃えて比較) |
| `plan: 単体参照 6 キー` | `core/tests/test_convert.cpp` | `sources` に `背景`(`file: C:/m/a.png`)、`BGM`(`local_file: C:/m/b.wav`)、`動画`(`path: C:/m/c.mp4`)、`マスク`(`image_path: C:/m/d.png`)、`マット`(`track_matte_path: C:/m/e.mov`)、`字幕`(`text_file: C:/m/f.txt`)。stat は全部 `RegularFile` | 値が `./assets/背景.png` `./assets/BGM.wav` `./assets/動画.mp4` `./assets/マスク.png` `./assets/マット.mov` `./assets/字幕.txt`。`copies` 6 件 |
| `plan: 入れ子の設定オブジェクト` | `core/tests/test_convert.cpp` | `transitions[0]` = `{name: フェード, settings: {path: C:/m/t.mov}}`、`groups[0]` = `{name: G, settings: {file: C:/m/g.png}}`、`sources[0]` = `{name: 画像, settings: {}, filters: [{name: 切り抜き, settings: {image_path: C:/m/k.png}}]}`、`sources[1]` = `{name: シーン, settings: {items: [{name: 画像, show_transition: {name: 登場, transition: {path: C:/m/s.webm}}}]}}` | `./assets/フェード.mov` `./assets/G.png` `./assets/切り抜き.png` `./assets/登場.webm` |
| `plan: 設定オブジェクトの外は触らない` | `core/tests/test_convert.cpp` | トップレベル `modules: {x: {path: C:/m/z.png}, scripts-tool: [{path: C:/s/a.lua, settings: {path: C:/m/z.png}}]}` | 値そのまま。`copies` `missing` とも 0 件 |
| `plan: 拡張子の二重付けをしない` | `core/tests/test_convert.cpp` | ソース名 `bg.PNG`、`file: C:/m/bg.png` | `./assets/bg.png` |
| `plan: url・相対パスは触らない` | `core/tests/test_convert.cpp` | `settings.url: https://x/y.html`、`file: ./assets/a.png` | 両方そのまま。`copies` `missing` とも 0 件 |
| `plan: スライドショーと VLC` | `core/tests/test_convert.cpp` | `files: [{value: C:/d/e/a.png, hidden: false}, {value: C:/o/b.png}]`、別ソースに `playlist: [{value: C:/v/m.mp4, selected: true}]` | `./assets/a.png` `./assets/b.png` `./assets/m.mp4`。`hidden` `selected` が残る |
| `plan: 同名別ファイルは連番` | `core/tests/test_convert.cpp` | 2 ソースとも名前 `画像`、`C:/a/x.png` と `C:/b/y.png` | `./assets/画像.png` と `./assets/画像_2.png` |
| `plan: 大小違いも衝突扱い` | `core/tests/test_convert.cpp` | ソース `BG`(`C:/a/1.png`)と `bg`(`C:/b/2.png`) | `./assets/BG.png` と `./assets/bg_2.png` |
| `plan: 同じファイルは共有` | `core/tests/test_convert.cpp` | ソース `背景1` と `背景2` が同じ `C:/m/bg.png`、スライドショーでも同じパスを 2 回 | 全部 `./assets/背景1.png`。`copies` 1 件 |
| `plan: 見つからない・フォルダ` | `core/tests/test_convert.cpp` | stat が `C:/m/none.png` に `Missing`、`C:/m/dir` に `Directory` | 値はそのまま。`missing` 2 件(`NotFound` / `IsDirectory`、ソース名つき) |
| `plan: 形が欠けていても落ちない` | `core/tests/test_convert.cpp` | `settings` 無しの要素、`name` 無しの要素、`sources` 無しの JSON、`files` が文字列 | 例外なし。`name` 無しの欠落は `source_name` が `""` |
| `plan: その他のキーと順番を保つ` | `core/tests/test_convert.cpp` | `name` `current_scene` `sources` `scene_order` の順の JSON | `converted.dump()` のキー順が同じ、`name` と `current_scene` は同じ値 |
| `export: 出力の形とバイト一致` | `core/tests/test_export.cpp` | 一時フォルダに素材 3 つ(うち 1 つは日本語名)、コレクション名 `本配信` | `<親>/本配信/本配信.json` と `assets/` の 3 ファイル。中身がバイト一致。JSON を読み直すと値が `./assets/...` |
| `export: フォルダ名の重複を避ける` | `core/tests/test_export.cpp` | `<親>/本配信/` を先に作る。別ケースで `<親>/本配信_2.zip` だけも作る | 1 つ目は `本配信_2`。2 つ目(`本配信/` と `本配信_2.zip` あり)は `本配信_3` |
| `export: 欠落を返す` | `core/tests/test_export.cpp` | 参照先の 1 つが存在しない | `missing` 1 件、`copied` は残りの件数、JSON の該当値は元のまま |
| `export: 進捗を呼ぶ` | `core/tests/test_export.cpp` | 素材 3 つ | `progress` が `(1,3)` `(2,3)` `(3,3)` の順に 3 回 |
| `export: 書けないと例外` | `core/tests/test_export.cpp` | `parent_dir` に既存の**ファイル**のパスを渡す(フォルダを作れない) | `ExportError` が投げられる |
| `export: zip を作る` | `core/tests/test_export.cpp` | `make_zip = true` | `<親>/本配信.zip` ができ、`zip_path` がそれを指す |
| `zip: 読み戻して一致` | `core/tests/test_zip.cpp` | `root/本配信.json` と `root/assets/背景.png` `root/assets/a.mp4` | エントリ名が `本配信.json` `assets/背景.png` `assets/a.mp4`、中身一致、全エントリ STORED、日本語名のエントリは `m_bit_flag & 0x0800` が非 0 |

## 実装ブロック

| ブロック | 対象タスク | 触るファイル | 確認コマンド |
|---|---|---|---|
| B1 | T1, T2 | `core/CMakeLists.txt` `core/include/portable/convert.hpp` `core/src/convert.cpp` `core/tests/test_main.cpp` `core/tests/test_convert.cpp` `scripts/test-core.ps1` `.gitignore` | `pwsh -NoProfile -File scripts/test-core.ps1` |
| B2 | T3, T4 | `core/CMakeLists.txt` `core/include/portable/export.hpp` `core/src/export.cpp` `core/include/portable/zip.hpp` `core/src/zip.cpp` `core/tests/test_export.cpp` `core/tests/test_zip.cpp` | `pwsh -NoProfile -File scripts/test-core.ps1` |
| B3 | T5 | template 一式(変更点の表の 1 行目)、`buildspec.json` `CMakeLists.txt` `src/plugin-main.c`(削除)`src/plugin-main.cpp` `.github/workflows/build-project.yaml` `.github/workflows/push.yaml` `.github/workflows/pr-pull.yaml` `.github/workflows/check-format.yaml`(削除)`.github/workflows/core-tests.yaml` `.gitignore` | PR の GitHub Actions で `windows-build` と `core-tests` が緑(`gh pr checks <PR> --watch`) |
| B4 | T6 | `src/plugin-main.cpp` `src/export-dialog.hpp` `src/export-dialog.cpp` `src/collection-file.hpp` `src/collection-file.cpp` `data/locale/en-US.ini` `data/locale/ja-JP.ini` `CMakeLists.txt` | PR の GitHub Actions で `windows-build` が緑(`gh pr checks <PR> --watch`) |

## 依存 / 前提

- ローカルのテストは WSL `Ubuntu-24.04`(ユーザ `max`)の g++ と `uvx`(cmake / ninja を pip パッケージから取る)を使う。Windows 側に MSVC は要らない
- プラグイン本体のビルドは GitHub Actions(template の `windows-build`)だけで行う
- 実機確認(H1)は OBS 31 系の Windows 2 台(書き出し用 PC と、プラグインを入れない受け取り用 PC)

## リスク / 降りる箇所

- **template の警告設定で依存ライブラリがビルドに落ちる** — `core/CMakeLists.txt` で依存を `SYSTEM` 扱いにして回避する。それでも `windows-build` が警告で落ちるなら、template 側の警告設定(`cmake/windows/compilerconfig.cmake`)は書き換えずに止めて報告する。
- **`current_collection_file()` が OBS の版で合わない** — B4 では実装してビルドを通すところまで(式は OBS 31 のソースに合わせてある)。合っているかは H1 の 3 で見る。
- **受け取り側の長いパス** — OBS 標準インポートの `CheckPath` は 512 バイトのバッファで絶対パスを作り、超えると `./` のまま残って開けない。出力名は 80 バイトで切るが、受け取り側で深いフォルダに置くと超えうる。H1 の 5 で見る。
- **H1(人の実機確認)**。書き出しは 書き出し用 PC の OBS(GitHub Actions の `windows-build` の成果物を入れる)、読み込みは 受け取り用 PC の**このプラグインを入れていない** OBS。
  1. コレクション A(日本語名のソース・画像・メディア・スライドショー・画像マスクフィルタ・表示トランジションにスティンガーを含み、**欠けた素材は無い**)を用意する。「ツール」メニューに「ポータブル書き出し…」が出ることを見る
  2. A に小さな変更(ソースを 1 つ動かす)を加えて**保存操作をせずに**書き出す。進捗ダイアログが出て、書き出し中も OBS の画面が固まらないことを見る
  3. 出力フォルダの JSON が A であり、2 の変更が入っていることを 受け取り用 PC で開いて確かめる
  4. 受け取り用 PC で「シーンコレクション → インポート」→ 不足ファイルのダイアログが**出ない**こと、見た目が 書き出し用 PC と同じことを見る。zip も展開して同じ確認をする
  5. 出力フォルダを日本語名を含む深いフォルダ(例 `D:\配信素材\受け取り\2026年\第1回の配信用\素材一式\`)に置いてもう一度 4
  6. コレクション B(A に**存在しないファイルを指す画像ソース**を 1 つ足したもの)を書き出す → 結果ダイアログの一覧にそのソースだけが出ること、
     受け取り用 PC でインポートすると不足ファイルのダイアログに**そのファイルだけ**が出ることを見る
