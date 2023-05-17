#include "Application.h"
#include "FileDialog.h"
#include "SUS.h"
#include "SusExporter.h"
#include "SusParser.h"
#include "ScoreConverter.h"
#include "UI.h"
#include "Constants.h"
#include "Utilities.h"
#include <Windows.h>

#undef min
#undef max

namespace MikuMikuWorld
{
	static MultiInputBinding* timelineModeBindings[] =
	{
		&config.input.timelineSelect,
		&config.input.timelineTap,
		&config.input.timelineHold,
		&config.input.timelineHoldMid,
		&config.input.timelineFlick,
		&config.input.timelineCritical,
		&config.input.timelineBpm,
		&config.input.timelineTimeSignature,
		&config.input.timelineHiSpeed,
	};

	ScoreEditor::ScoreEditor()
	{
		renderer = std::make_unique<Renderer>();
		context.audio.initAudio();

		exportComment = IO::concat("This file was generated by " APP_NAME, Application::getAppVersion().c_str(), " ");
	}

	void ScoreEditor::update()
	{
		drawMenubar();
		drawToolbar();

		if (!ImGui::GetIO().WantCaptureKeyboard)
		{
			if (ImGui::IsAnyPressed(config.input.create)) Application::windowState.resetting = true;
			if (ImGui::IsAnyPressed(config.input.open))
			{
				Application::windowState.resetting = true;
				Application::windowState.shouldPickScore = true;
			}

			if (ImGui::IsAnyPressed(config.input.openSettings)) settingsWindow.open = true;
			if (ImGui::IsAnyPressed(config.input.openHelp)) ShellExecuteW(0, 0, L"https://github.com/crash5band/MikuMikuWorld/wiki", 0, 0, SW_SHOW);

			if (ImGui::IsAnyPressed(config.input.save)) trySave(context.workingData.filename);
			if (ImGui::IsAnyPressed(config.input.saveAs)) saveAs();
			if (ImGui::IsAnyPressed(config.input.exportSus)) exportSus();
			if (ImGui::IsAnyPressed(config.input.togglePlayback)) timeline.togglePlaying(context);
			if (ImGui::IsAnyPressed(config.input.previousTick, true)) timeline.previousTick(context);
			if (ImGui::IsAnyPressed(config.input.nextTick, true)) timeline.nextTick(context);
			if (ImGui::IsAnyPressed(config.input.selectAll)) context.selectAll();
			if (ImGui::IsAnyPressed(config.input.deleteSelection)) context.deleteSelection();
			if (ImGui::IsAnyPressed(config.input.cutSelection)) context.cutSelection();
			if (ImGui::IsAnyPressed(config.input.copySelection)) context.copySelection();
			if (ImGui::IsAnyPressed(config.input.paste)) context.paste(false);
			if (ImGui::IsAnyPressed(config.input.flipPaste)) context.paste(true);
			if (ImGui::IsAnyPressed(config.input.cancelPaste)) context.cancelPaste();
			if (ImGui::IsAnyPressed(config.input.flip)) context.flipSelection();
			if (ImGui::IsAnyPressed(config.input.undo)) context.undo();
			if (ImGui::IsAnyPressed(config.input.redo)) context.redo();
			if (ImGui::IsAnyPressed(config.input.zoomOut, true)) timeline.setZoom(timeline.getZoom() - 0.25f);
			if (ImGui::IsAnyPressed(config.input.zoomIn, true)) timeline.setZoom(timeline.getZoom() + 0.25f);
			if (ImGui::IsAnyPressed(config.input.decreaseNoteSize, true)) edit.noteWidth = std::clamp(edit.noteWidth - 1, MIN_NOTE_WIDTH, MAX_NOTE_WIDTH);
			if (ImGui::IsAnyPressed(config.input.increaseNoteSize, true)) edit.noteWidth = std::clamp(edit.noteWidth + 1, MIN_NOTE_WIDTH, MAX_NOTE_WIDTH);
			if (ImGui::IsAnyPressed(config.input.shrinkDown)) context.shrinkSelection(Direction::Down);
			if (ImGui::IsAnyPressed(config.input.shrinkUp)) context.shrinkSelection(Direction::Up);

			for (int i = 0; i < (int)TimelineMode::TimelineModeMax; ++i)
				if (ImGui::IsAnyPressed(*timelineModeBindings[i])) timeline.changeMode((TimelineMode)i, edit);
		}

		if (config.timelineWidth != timeline.laneWidth)
			timeline.laneWidth = config.timelineWidth;

		if (config.backgroundBrightness != timeline.background.getBrightness())
			timeline.background.setBrightness(config.backgroundBrightness);

		if (settingsWindow.open)
		{
			ImGui::OpenPopup(MODAL_TITLE("settings"));
			settingsWindow.open = false;
		}
		settingsWindow.update();

		if (aboutDialog.open)
		{
			ImGui::OpenPopup(MODAL_TITLE("about"));
			aboutDialog.open = false;
		}
		aboutDialog.update();

		ImGui::Begin(IMGUI_TITLE(ICON_FA_MUSIC, "notes_timeline"), NULL, ImGuiWindowFlags_Static | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
		timeline.update(context, edit, renderer.get());
		ImGui::End();

		if (ImGui::Begin(IMGUI_TITLE(ICON_FA_BUG, "debug"), NULL))
		{
			timeline.debug();
		}
		ImGui::End();

		if (ImGui::Begin(IMGUI_TITLE(ICON_FA_ALIGN_LEFT, "chart_properties"), NULL, ImGuiWindowFlags_Static))
		{
			propertiesWindow.update(context);
		}
		ImGui::End();

		if (ImGui::Begin(IMGUI_TITLE(ICON_FA_WRENCH, "settings"), NULL, ImGuiWindowFlags_Static))
		{
			optionsWindow.update(context, edit, timeline.getMode());
		}
		ImGui::End();

		if (ImGui::Begin(IMGUI_TITLE(ICON_FA_DRAFTING_COMPASS, "presets"), NULL, ImGuiWindowFlags_Static))
		{
			presetsWindow.update(context, presetManager);
		}
		ImGui::End();

		if (showImGuiDemoWindow)
			ImGui::ShowDemoWindow(&showImGuiDemoWindow);
	}

	void ScoreEditor::create()
	{
		context.score = {};
		context.workingData = {};
		context.history.clear();
		context.scoreStats.reset();
		context.audio.disposeBGM();
		context.upToDate = true; // new score; nothing to save
	}

	void ScoreEditor::loadScore(std::string filename)
	{
		std::string extension = IO::File::getFileExtension(filename);
		std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);

		// backup next note ID in case of an import failure
		int nextIdBackup = nextID;
		try
		{
			resetNextID();
			if (extension == SUS_EXTENSION)
			{
				SusParser susParser;
				context.score = ScoreConverter::susToScore(susParser.parse(filename));
				context.workingData.filename = "";
			}
			else if (extension == MMWS_EXTENSION)
			{
				context.score = deserializeScore(filename);
				context.workingData.filename = filename;
			}

			context.workingData.title = context.score.metadata.title;
			context.workingData.designer = context.score.metadata.author;
			context.workingData.artist = context.score.metadata.artist;
			context.workingData.musicOffset = context.score.metadata.musicOffset;
			context.workingData.musicFilename = context.score.metadata.musicFile;
			context.workingData.jacket.load(context.score.metadata.jacketFile);

			context.audio.changeBGM(context.workingData.musicFilename);
			context.audio.setBGMOffset(0, context.workingData.musicOffset);

			context.history.clear();
			context.scoreStats.calculateStats(context.score);
			timeline.calculateMaxOffsetFromScore(context.score);

			UI::setWindowTitle((context.workingData.filename.size() ? IO::File::getFilename(context.workingData.filename) : windowUntitled) + "*");
			context.upToDate = context.workingData.filename.size();
		}
		catch (std::runtime_error& err)
		{
			nextID = nextIdBackup;

			std::string errMsg = "An error occured while reading the score file.\n" + std::string(err.what());
			IO::messageBox(APP_NAME, errMsg, IO::MessageBoxButtons::Ok, IO::MessageBoxIcon::Error);
		}
	}

	void ScoreEditor::open()
	{
		std::string filename;
		if (IO::FileDialog::openFile(filename, IO::FileType::ScoreFile))
			loadScore(filename);
	}

	bool ScoreEditor::trySave(std::string filename)
	{
		try
		{
			if (!filename.size())
				return saveAs();
			else
				return save(filename);
		}
		catch (std::runtime_error& error)
		{
			std::string msg{ "An error occured while trying to save the chart.\n" };
			msg.append(error.what());
			IO::messageBox(APP_NAME, msg.c_str(), IO::MessageBoxButtons::Ok, IO::MessageBoxIcon::Error);
		}

		return false;
	}

	bool ScoreEditor::save(std::string filename)
	{
		context.score.metadata.title = context.workingData.title;
		context.score.metadata.author = context.workingData.designer;
		context.score.metadata.artist = context.workingData.artist;
		context.score.metadata.musicFile = context.workingData.musicFilename;
		context.score.metadata.musicOffset = context.workingData.musicOffset;
		context.score.metadata.jacketFile = context.workingData.jacket.getFilename();
		serializeScore(context.score, filename);

		UI::setWindowTitle(IO::File::getFilename(filename));
		context.upToDate = true;

		return true;
	}

	bool ScoreEditor::saveAs()
	{
		std::string filename;
		if (IO::FileDialog::saveFile(filename, IO::FileType::MMWSFile))
		{
			context.workingData.filename = filename;
			return save(context.workingData.filename);
		}

		return false;
	}

	void ScoreEditor::exportSus()
	{
		std::string filename;
		if (IO::FileDialog::saveFile(filename, IO::FileType::SUSFile))
		{
			SusExporter exporter;
			SUS sus = ScoreConverter::scoreToSus(context.score);
			exporter.dump(sus, filename, exportComment);
		}
	}

	void ScoreEditor::drawMenubar()
	{
		ImGui::BeginMainMenuBar();
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10, 3));

		if (ImGui::BeginMenu(getString("file")))
		{
			if (ImGui::MenuItem(getString("new"), ToShortcutString(config.input.create)))
				Application::windowState.resetting = true;

			if (ImGui::MenuItem(getString("open"), ToShortcutString(config.input.open)))
			{
				Application::windowState.resetting = true;
				Application::windowState.shouldPickScore = true;
			}

			ImGui::Separator();
			if (ImGui::MenuItem(getString("save"), ToShortcutString(config.input.save)))
				trySave(context.workingData.filename);

			if (ImGui::MenuItem(getString("save_as"), ToShortcutString(config.input.saveAs)))
				saveAs();

			if (ImGui::MenuItem(getString("export"), ToShortcutString(config.input.exportSus)))
				exportSus();

			ImGui::Separator();
			if (ImGui::MenuItem(getString("exit"), ToShortcutString(ImGuiKey_F4, ImGuiKeyModFlags_Alt)))
				Application::windowState.closing = true;

			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu(getString("edit")))
		{
			if (ImGui::MenuItem(getString("undo"), ToShortcutString(config.input.undo), false, context.history.hasUndo()))
				context.undo();

			if (ImGui::MenuItem(getString("redo"), ToShortcutString(config.input.redo), false, context.history.hasRedo()))
				context.redo();

			ImGui::Separator();
			if (ImGui::MenuItem(getString("delete"), ToShortcutString(config.input.deleteSelection), false, context.selectedNotes.size()))
				context.deleteSelection();

			if (ImGui::MenuItem(getString("cut"), ToShortcutString(config.input.cutSelection), false, context.selectedNotes.size()))
				context.cutSelection();

			if (ImGui::MenuItem(getString("copy"), ToShortcutString(config.input.copySelection), false, context.selectedNotes.size()))
				context.copySelection();

			if (ImGui::MenuItem(getString("paste"), ToShortcutString(config.input.paste)))
				context.paste(false);

			ImGui::Separator();
			if (ImGui::MenuItem(getString("select_all"), ToShortcutString(config.input.selectAll)))
				context.selectAll();

			ImGui::Separator();
			if (ImGui::MenuItem(getString("settings"), ToShortcutString(config.input.openSettings)))
				settingsWindow.open = true;

			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu(getString("view")))
		{
			ImGui::MenuItem(getString("show_step_outlines"), NULL, &timeline.drawHoldStepOutlines);
			ImGui::MenuItem(getString("playback_auto_scroll"), NULL, &config.followCursorInPlayback);
			ImGui::MenuItem(getString("return_to_last_tick"), NULL, &config.returnToLastSelectedTickOnPause);

			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu(getString("window")))
		{
			if (ImGui::MenuItem(getString("vsync"), NULL, &config.vsync))
				glfwSwapInterval(config.vsync);

			ImGui::MenuItem("ImGui Demo Window", NULL, &showImGuiDemoWindow);

			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu(getString("help")))
		{
			if (ImGui::MenuItem(getString("help"), ToShortcutString(config.input.openHelp)))
				ShellExecuteW(0, 0, L"https://github.com/crash5band/MikuMikuWorld/wiki", 0, 0, SW_SHOW);

			if (ImGui::MenuItem(getString("about")))
				aboutDialog.open = true;

			ImGui::EndMenu();
		}

		std::string fps = IO::formatString("%.3fms (%.1fFPS)", ImGui::GetIO().DeltaTime * 1000, ImGui::GetIO().Framerate);
		ImGui::SetCursorPosX(ImGui::GetWindowSize().x - ImGui::CalcTextSize(fps.c_str()).x - ImGui::GetStyle().WindowPadding.x);
		ImGui::Text(fps.c_str());

		ImGui::PopStyleVar();
		ImGui::EndMainMenuBar();
	}

	void ScoreEditor::drawToolbar()
	{
		ImGuiViewport* viewport = ImGui::GetMainViewport();
		ImVec2 toolbarSize{ viewport->WorkSize.x, UI::toolbarBtnSize.y + ImGui::GetStyle().WindowPadding.y + 5 };

		// keep toolbar on top in main viewport
		ImGui::SetNextWindowViewport(viewport->ID);
		ImGui::SetNextWindowPos(viewport->WorkPos);
		ImGui::SetNextWindowSize(toolbarSize, ImGuiCond_Always);

		// toolbar style
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{ 0.0f, 0.0f, 0.0f, 0.0f });
		ImGui::PushStyleColor(ImGuiCol_Separator, ImGui::GetStyleColorVec4(ImGuiCol_WindowBg));
		ImGui::PushStyleColor(ImGuiCol_WindowBg, ImGui::GetStyleColorVec4(ImGuiCol_MenuBarBg));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
		ImGui::Begin("##app_toolbar", NULL, ImGuiWindowFlags_Toolbar);
		
		if (UI::toolbarButton(ICON_FA_FILE, getString("new"), ToShortcutString(config.input.create)))
		{
			Application::windowState.resetting = true;
		}

		if (UI::toolbarButton(ICON_FA_FOLDER_OPEN, getString("open"), ToShortcutString(config.input.open)))
		{
			Application::windowState.resetting = true;
			Application::windowState.shouldPickScore = true;
		}

		if (UI::toolbarButton(ICON_FA_SAVE, getString("save"), ToShortcutString(config.input.save)))
			trySave(context.workingData.filename);

		if (UI::toolbarButton(ICON_FA_FILE_EXPORT, getString("export"), ToShortcutString(config.input.exportSus)))
			exportSus();

		UI::toolbarSeparator();

		if (UI::toolbarButton(ICON_FA_CUT, getString("cut"), ToShortcutString(config.input.cutSelection), context.selectedNotes.size() > 0))
			context.cutSelection();

		if (UI::toolbarButton(ICON_FA_COPY, getString("copy"), ToShortcutString(config.input.copySelection), context.selectedNotes.size() > 0))
			context.copySelection();

		if (UI::toolbarButton(ICON_FA_PASTE, getString("paste"), ToShortcutString(config.input.paste)))
			context.paste(false);

		UI::toolbarSeparator();

		if (UI::toolbarButton(ICON_FA_UNDO, getString("undo"), ToShortcutString(config.input.undo), context.history.hasUndo()))
			context.undo();

		if (UI::toolbarButton(ICON_FA_REDO, getString("redo"), ToShortcutString(config.input.redo), context.history.hasRedo()))
			context.redo();

		UI::toolbarSeparator();

		for (int i = 0; i < TXT_ARR_SZ(timelineModes); ++i)
		{
			std::string img{ "timeline_" };
			img.append(timelineModes[i]);
			if (i == (int)TimelineMode::InsertFlick)
			{
				if (edit.flickType == FlickType::Left) img.append("_left");
				if (edit.flickType == FlickType::Right) img.append("_right");
			}
			else if (i == (int)TimelineMode::InsertLongMid)
			{
				if (edit.stepType == HoldStepType::Hidden) img.append("_hidden");
				if (edit.stepType == HoldStepType::Skip) img.append("_skip");
			}

			if (UI::toolbarImageButton(img.c_str(), getString(timelineModes[i]), ToShortcutString(*timelineModeBindings[i]), true, (int)timeline.getMode() == i))
				timeline.changeMode((TimelineMode)i, edit);
		}

		ImGui::PopStyleColor(3);
		ImGui::PopStyleVar(2);
		ImGui::End();
	}
}