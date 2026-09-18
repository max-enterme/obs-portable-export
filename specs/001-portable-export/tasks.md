---
feature: portable-export
---

# タスク — 素材ごとのポータブル書き出し(Windows)

> tasks.md — 実作業の分解。各タスクは GitHub sub-issue(type: Task)と対応。
> `- [ ]` 未完 / `- [x]` 完了。採番後に `<!-- #12 -->` の形で sub-issue 番号を書く。
> **未採番のうちはコメントごと書かない**(空のプレースホルダは採番済みと誤読される。SPEC-OPS §04)。

- [ ] T1: core ライブラリの骨組みとテスト実行(core/CMakeLists.txt・doctest・scripts/test-core.ps1)  <!-- #2 -->
- [ ] T2: 変換規則 plan_conversion(設定オブジェクトの走査・パス判定・出力名・衝突・欠落・UTF-8 パス変換)とテスト  <!-- #3 -->
- [ ] T3: 書き出し export_collection(フォルダ名の重複回避・コピー・JSON 出力・進捗)とテスト  <!-- #4 -->
- [ ] T4: zip 作成 write_zip(無圧縮・UTF-8 名・fopen を使わない)とテスト  <!-- #5 -->
- [ ] T5: obs-plugintemplate の取り込みと CI(Windows ビルドのみ・core-tests ワークフロー・メニュー項目だけの仮プラグイン)  <!-- #6 -->
- [ ] T6: プラグイン UI(出力先ダイアログ・保存待ち・別スレッドで書き出し・結果と欠落一覧・終了時の待ち合わせ)(T3・T4・T5 の後)  <!-- #7 -->
- [ ] H1: **人手**: 書き出し用 PC で書き出し → 受け取り用 PC のプラグイン無し OBS でインポート。plan.md「リスク / 降りる箇所」の H1 の 1〜6 を確かめる(T6 の後)  <!-- #8 -->
