/*
Portable Export
Copyright (C) 2026 max-enterme 112470175+max-enterme@users.noreply.github.com

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>
*/

#pragma once

class QWidget;

// メニューから呼ぶ。出力先ダイアログ〜結果表示まで。
void run_export(QWidget *parent);

// obs_module_unload から呼ぶ。書き出し中なら終わるまで待つ。
void export_shutdown();
