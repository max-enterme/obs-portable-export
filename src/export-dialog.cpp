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

#include "export-dialog.hpp"

#include <memory>
#include <string>
#include <thread>

#include <QCheckBox>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMetaObject>
#include <QPointer>
#include <QProgressDialog>
#include <QPushButton>
#include <QString>
#include <QUrl>
#include <QVBoxLayout>
#include <Qt>

#include <fstream>

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <util/config-file.h>

#include "collection-file.hpp"
#include "portable/export.hpp"

namespace {

QString qtext(const char *key)
{
	return QString::fromUtf8(obs_module_text(key));
}

struct ExportJob {
	std::thread thread;
};

// 以下はどれも UI スレッドだけが読み書きする(ワーカースレッドは g_job を作る前に一度
// 触るだけで、以後は QMetaObject::invokeMethod(parent, ..., Qt::QueuedConnection) 越しにしか
// UI スレッドとやり取りしない)。
// g_job: 書き出しスレッドが生きている間だけ存在する。
std::unique_ptr<ExportJob> g_job;
// g_export_running: OK を押した瞬間(obs_frontend_save() を呼ぶ前)から書き出し完了まで true。
// g_job より広い区間をカバーする二重起動ガード(「OK を押してから継続ラムダが走るまで」の窓を塞ぐ)。
bool g_export_running = false;
// g_progress: 進捗ダイアログ。ワーカースレッドからは絶対に読み書きしない
// (QPointer はスレッドセーフではないため)。ワーカー側は progress コールバックの中で
// receiver に寿命の確実な parent(メインウィンドウ)を指定し、実際にダイアログを触るのは
// GUI スレッドで実行されるラムダの中だけにする。
QPointer<QProgressDialog> g_progress;
// g_finished: 完了処理(finish_job)が走った後に届いた古い進捗更新を無視するためのフラグ。
// shared_ptr の参照カウントはスレッドセーフだが、中身の bool を読み書きするのは GUI スレッドだけ。
std::shared_ptr<bool> g_finished;

// 書き出しの完了・失敗・強制終了(OBS 終了時)のどの経路からも呼べる。二重に呼んでも安全。
void finish_job()
{
	if (g_finished)
		*g_finished = true;
	if (g_job) {
		if (g_job->thread.joinable())
			g_job->thread.join();
		g_job.reset();
	}
	g_export_running = false;
	if (g_progress)
		g_progress->close();
	g_progress = nullptr;
	g_finished.reset();
}

void show_result(QWidget *parent, const portable::ExportResult &result, bool made_zip)
{
	QString body = qtext("PortableExport.OutDir")
			       .arg(QString::fromUtf8(portable::utf8_from_path(result.out_dir).c_str()));
	body += "\n" + qtext("PortableExport.Copied").arg(QString::number(result.copied));
	if (made_zip)
		body += "\n" + qtext("PortableExport.ZipPath")
					.arg(QString::fromUtf8(portable::utf8_from_path(result.zip_path).c_str()));
	if (!result.missing.empty())
		body += "\n" + qtext("PortableExport.Missing").arg(QString::number(result.missing.size()));

	QMessageBox box(QMessageBox::Information, qtext("PortableExport.Done"), body, QMessageBox::NoButton, parent);
	QPushButton *open_button = box.addButton(qtext("PortableExport.OpenFolder"), QMessageBox::ActionRole);
	box.addButton(QMessageBox::Ok);

	if (!result.missing.empty()) {
		QString detail;
		for (const auto &missing : result.missing) {
			const char *reason_key = missing.reason == portable::MissingReason::NotFound
							  ? "PortableExport.Reason.NotFound"
							  : "PortableExport.Reason.IsDirectory";
			detail += QString::fromUtf8(missing.source_name.c_str()) + "\t" +
				  QString::fromUtf8(missing.original_path.c_str()) + "\t" + qtext(reason_key) + "\n";
		}
		box.setDetailedText(detail);
	}

	box.exec();

	if (box.clickedButton() == open_button)
		QDesktopServices::openUrl(
			QUrl::fromLocalFile(QString::fromUtf8(portable::utf8_from_path(result.out_dir).c_str())));
}

// 出力先ダイアログ。出力先の親フォルダと「zip も作る」を選ばせる。
class ExportDialog : public QDialog {
	Q_OBJECT

public:
	explicit ExportDialog(QWidget *parent) : QDialog(parent)
	{
		setWindowTitle(qtext("PortableExport.Title"));

		dir_edit = new QLineEdit(this);
		auto *browse_button = new QPushButton(qtext("PortableExport.Browse"), this);
		zip_check = new QCheckBox(qtext("PortableExport.MakeZip"), this);
		buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);

		config_t *user_config = obs_frontend_get_user_config();
		if (user_config) {
			const char *last_dir = config_get_string(user_config, "PortableExport", "LastDir");
			if (last_dir)
				dir_edit->setText(QString::fromUtf8(last_dir));
			zip_check->setChecked(config_get_bool(user_config, "PortableExport", "MakeZip"));
		}

		auto *dir_row = new QHBoxLayout();
		dir_row->addWidget(new QLabel(qtext("PortableExport.ParentDir"), this));
		dir_row->addWidget(dir_edit);
		dir_row->addWidget(browse_button);

		auto *layout = new QVBoxLayout(this);
		layout->addLayout(dir_row);
		layout->addWidget(zip_check);
		layout->addWidget(buttons);

		connect(browse_button, &QPushButton::clicked, this, &ExportDialog::browse);
		connect(dir_edit, &QLineEdit::textChanged, this, &ExportDialog::update_ok_enabled);
		connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
		connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

		update_ok_enabled();
	}

	QString parent_dir() const { return dir_edit->text(); }
	bool make_zip() const { return zip_check->isChecked(); }

private slots:
	void browse()
	{
		QString dir = QFileDialog::getExistingDirectory(this, qtext("PortableExport.Browse"), dir_edit->text());
		if (!dir.isEmpty())
			dir_edit->setText(dir);
	}

	void update_ok_enabled()
	{
		QString dir = dir_edit->text();
		bool valid = !dir.isEmpty() && QFileInfo(dir).isDir();
		buttons->button(QDialogButtonBox::Ok)->setEnabled(valid);
	}

private:
	QLineEdit *dir_edit = nullptr;
	QCheckBox *zip_check = nullptr;
	QDialogButtonBox *buttons = nullptr;
};

// obs_frontend_save() は保存を Qt::QueuedConnection で予約するだけなので、続きは
// さらに Qt::QueuedConnection で後ろに積む。これで 3 で読むファイルは保存後のものになる。
void continue_export_after_save(QWidget *parent, QString parent_dir_str, bool make_zip)
{
	// 二重起動ガードの二段目(g_export_running だけでは「OK を押してから obs_frontend_save() の
	// キューが空になるまで」の窓しか塞げないため、実際に g_job を作る直前にもう一度確認する)。
	if (g_job)
		return;

	std::filesystem::path collection_path = current_collection_file();
	if (collection_path.empty()) {
		g_export_running = false;
		QMessageBox::critical(parent, qtext("PortableExport.Failed"),
				       qtext("PortableExport.Failed") + "\n" + qtext("PortableExport.NoCollection"));
		return;
	}

	portable::Json collection;
	{
		std::ifstream ifs(collection_path, std::ios::binary);
		if (!ifs) {
			g_export_running = false;
			QMessageBox::critical(parent, qtext("PortableExport.Failed"),
					       qtext("PortableExport.Failed") + "\n" +
						       qtext("PortableExport.ReadFailed")
							       .arg(QString::fromUtf8(
								       portable::utf8_from_path(collection_path).c_str())));
			return;
		}
		try {
			collection = portable::Json::parse(ifs);
		} catch (const std::exception &) {
			g_export_running = false;
			QMessageBox::critical(parent, qtext("PortableExport.Failed"),
					       qtext("PortableExport.Failed") + "\n" +
						       qtext("PortableExport.ReadFailed")
							       .arg(QString::fromUtf8(
								       portable::utf8_from_path(collection_path).c_str())));
			return;
		}
	}

	auto *progress = new QProgressDialog(qtext("PortableExport.Progress"), QString(), 0, 0, parent);
	progress->setWindowTitle(qtext("PortableExport.Title"));
	progress->setWindowModality(Qt::WindowModal);
	progress->setCancelButton(nullptr);
	progress->setMinimumDuration(0);
	// 最後の素材をコピーした瞬間に autoReset/autoClose で消えてしまわないようにする。
	// JSON 書き出しと zip 作成がまだ残っているため、閉じるのは finish_job() に任せる。
	progress->setAutoReset(false);
	progress->setAutoClose(false);
	// close() を「隠すだけ」で終わらせず、確実に破棄する。QPointer(g_progress)の
	// null 化はこの削除に連動するので、g_progress の管理と二重にならないよう
	// 手動 deleteLater() は呼ばない。
	progress->setAttribute(Qt::WA_DeleteOnClose);
	progress->show();

	// これ以降、進捗ダイアログと完了フラグは g_progress / g_finished(=UI スレッド専有)経由でだけ触る。
	g_progress = progress;
	g_finished = std::make_shared<bool>(false);
	std::shared_ptr<bool> finished = g_finished; // ワーカースレッドへ渡す分。shared_ptr のコピー自体はスレッドセーフ。

	portable::ExportOptions options;
	options.parent_dir = portable::path_from_utf8(parent_dir_str.toUtf8().constData());
	options.make_zip = make_zip;

	g_job = std::make_unique<ExportJob>();
	g_job->thread = std::thread([collection, options, parent, finished, make_zip]() mutable {
		try {
			portable::ExportResult result = portable::export_collection(
				collection, options, [parent, finished](size_t done, size_t total) {
					// ワーカースレッドからは QPointer(g_progress)を一切読み書きしない。
					// receiver には寿命の確実な parent(メインウィンドウ)を渡す
					// (OBS_FRONTEND_EVENT_EXIT で書き出し完了を待ってから
					// メインウィンドウが破棄されるようにしてあるため常に生存している)。
					// 進捗ダイアログの生死は GUI スレッドで実行されるこの内側の
					// ラムダの中で g_progress を見てだけ判断する。
					QMetaObject::invokeMethod(
						parent,
						[finished, done, total]() {
							if (*finished || !g_progress)
								return;
							g_progress->setRange(0, static_cast<int>(total));
							g_progress->setValue(static_cast<int>(done));
						},
						Qt::QueuedConnection);
				});

			QMetaObject::invokeMethod(
				parent,
				[parent, result, make_zip, finished]() {
					// OBS 終了時(OBS_FRONTEND_EVENT_EXIT)に export_shutdown() が
					// 先に join を終えている場合、このラムダは投函済みのまま残る。
					// そのまま結果ダイアログを開くと、終了処理が OK を押すまで止まる。
					if (*finished)
						return;
					finish_job();
					show_result(parent, result, make_zip);
				},
				Qt::QueuedConnection);
		} catch (const std::exception &e) {
			std::string message = e.what();
			QMetaObject::invokeMethod(
				parent,
				[parent, message, finished]() {
					if (*finished)
						return;
					finish_job();
					QMessageBox::critical(parent, qtext("PortableExport.Failed"),
							       qtext("PortableExport.Failed") + "\n" +
								       QString::fromUtf8(message.c_str()));
				},
				Qt::QueuedConnection);
		}
	});
}

} // namespace

void run_export(QWidget *parent)
{
	// 「OK を押してから継続ラムダ(continue_export_after_save)が走るまで」の窓は
	// g_job ではなく g_export_running で塞ぐ(g_job はまだ作られていないため)。
	if (g_export_running) {
		QMessageBox::information(parent, qtext("PortableExport.Title"), qtext("PortableExport.Busy"));
		return;
	}

	ExportDialog dialog(parent);
	if (dialog.exec() != QDialog::Accepted)
		return;

	QString parent_dir_str = dialog.parent_dir();
	bool make_zip = dialog.make_zip();

	config_t *user_config = obs_frontend_get_user_config();
	if (user_config) {
		config_set_string(user_config, "PortableExport", "LastDir", parent_dir_str.toUtf8().constData());
		config_set_bool(user_config, "PortableExport", "MakeZip", make_zip);
		config_save(user_config);
	}

	// obs_frontend_save() を呼ぶ前に印を立てる。ここから継続ラムダが走って g_job が
	// 作られるまでの間に二重に OK を押されても、この印だけで弾ける。
	g_export_running = true;

	obs_frontend_save();

	QMetaObject::invokeMethod(
		parent,
		[parent, parent_dir_str, make_zip]() { continue_export_after_save(parent, parent_dir_str, make_zip); },
		Qt::QueuedConnection);
}

void export_shutdown()
{
	// obs_module_unload からも、OBS_FRONTEND_EVENT_EXIT のハンドラ(plugin-main.cpp)からも
	// 呼ばれる。g_job が無ければ何もしない(冪等)。
	finish_job();
}

#include "export-dialog.moc"
