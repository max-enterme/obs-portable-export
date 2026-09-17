# obs-portable-export

> Export an OBS scene collection **together with its media files**, in a form that opens on another PC with **plain OBS — no plugin needed on the receiving side**. (Work in progress)

OBS のシーンコレクションを、**素材ファイルごと、別の PC の素の OBS でそのまま開ける形**で書き出す OBS プラグイン(開発中)。

## なぜ作るか

- OBS 標準の「シーンコレクション → エクスポート」は JSON だけを書き出す。素材は元の絶対パスを指したままなので、別の PC では全部「見つからない」になる。
- 既成プラグイン [Scene Collection Manager](https://github.com/exeldro/obs-scene-collection-manager) の Export は素材も集めるが、パスを `sources/<ソース名>/settings/file/xxx.png` と **`./` 無し**で書く。そのため受け取り側も同プラグインの Import を使わないと開けない。
- OBS 標準のインポート([`frontend/importers/studio.cpp`](https://github.com/obsproject/obs-studio/blob/master/frontend/importers/studio.cpp) の `TranslatePaths`)は、**`./` で始まる文字列だけ**を「読み込んだ JSON のフォルダ基準」の絶対パスに直す。
  → `./assets/...` で書けば、受け取り側は**何も入れずに**標準インポートで開ける。

## 変換の中身(予定)

- 書き換え対象: `settings.file` / `settings.local_file` / `settings.path`(ソース名でリネームして `assets/` へ)、スライドショーの `settings.files[].value`(元のファイル名のまま `assets/` へ)
- 同名で中身が違うファイルは `_2` などの連番で衝突回避。同じファイルの複数参照は1コピーを共有
- transitions とグループ内のソースも対象

元になっているのは、作者が配信運営で使っている同等の変換 CLI。

## ライセンス

[GPL-2.0](LICENSE)(OBS Studio に合わせる)
