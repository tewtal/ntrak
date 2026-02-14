#include "ntrak/ui/QuickGuidePanel.hpp"

#include <imgui.h>

namespace ntrak::ui {

QuickGuidePanel::QuickGuidePanel(app::AppState& appState) : appState_(appState) {}

void QuickGuidePanel::draw() {
    if (ImGui::BeginTabBar("QuickGuideTabs")) {
        if (ImGui::BeginTabItem("Overview")) {
            drawOverview();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Effects")) {
            drawEffectsReference();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Shortcuts")) {
            drawKeyboardShortcuts();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
}

void QuickGuidePanel::drawOverview() {
    ImGui::TextWrapped("Welcome to ntrak - a modern music tracker for creating authentic SNES music!");
    ImGui::Spacing();
    ImGui::Separator();

    ImGui::SeparatorText("What is ntrak?");
    ImGui::TextWrapped(
        "ntrak is a music tracker designed specifically for composing music for the Super Nintendo "
        "Entertainment System (SNES). It allows you to create authentic chiptune music using the SNES's "
        "8-channel audio processor with support for different game engines like Super Metroid, "
        "The Legend of Zelda: A Link to the Past, and Super Mario World.");
    ImGui::Spacing();

    ImGui::SeparatorText("Main Panels");
    if (ImGui::BeginTable("PanelsTable", 2,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY,
                          ImVec2(0.0f, 0.0f))) {
        ImGui::TableSetupColumn("Panel", ImGuiTableColumnFlags_WidthFixed, 150.0f);
        ImGui::TableSetupColumn("Description", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted("Pattern Editor");
        ImGui::TableSetColumnIndex(1);
        ImGui::TextWrapped(
            "Main workspace for composing music. Shows 8 channels where you enter notes, "
            "instruments, volumes, and effects in a grid format.");

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted("Control Panel");
        ImGui::TableSetColumnIndex(1);
        ImGui::TextWrapped(
            "Playback controls (Play/Stop) and engine selection. Choose your target SNES game engine here.");

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted("Assets Panel");
        ImGui::TableSetColumnIndex(1);
        ImGui::TextWrapped(
            "Manage instruments and samples. Import WAV/BRR audio, create instruments with ADSR envelopes, "
            "and preview sounds.");

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted("Sequence Editor");
        ImGui::TableSetColumnIndex(1);
        ImGui::TextWrapped(
            "Define song structure. Arrange patterns in order, add loops, and control playback flow.");

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted("Project Panel");
        ImGui::TableSetColumnIndex(1);
        ImGui::TextWrapped(
            "Project overview showing songs, instruments, and samples. Switch between multiple songs "
            "in a project here.");

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted("Build Panel");
        ImGui::TableSetColumnIndex(1);
        ImGui::TextWrapped(
            "Configure optimization settings, including optional ARAM compaction to reduce holes "
            "and NSPC upload segment count.");

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted("ARAM Usage Panel");
        ImGui::TableSetColumnIndex(1);
        ImGui::TextWrapped(
            "Visual representation of Audio RAM usage. See how your music uses the SNES's limited "
            "memory with color-coded regions.");

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted("SPC Player Panel");
        ImGui::TableSetColumnIndex(1);
        ImGui::TextWrapped("Load and play external SPC files for reference or testing.");

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted("SPC Info Panel");
        ImGui::TableSetColumnIndex(1);
        ImGui::TextWrapped("Shows real-time technical information during playback (for debugging).");

        ImGui::EndTable();
    }

    ImGui::Spacing();
    ImGui::SeparatorText("Quick Start");
    ImGui::TextWrapped("1. Import Samples: Go to Assets Panel > Samples tab > Import WAV");
    ImGui::TextWrapped("2. Create Instruments: Assets Panel > Instruments tab > New > Configure ADSR");
    ImGui::TextWrapped("3. Compose Pattern: Click in Pattern Editor > Use keyboard keys (ZSXDCVGB...) to enter notes");
    ImGui::TextWrapped("4. Build Sequence: Sequence Editor > Add patterns to define song structure");
    ImGui::TextWrapped("5. Play: Press F5 to play from beginning, F6 to play from current pattern, F8 to stop");
    ImGui::Spacing();

    ImGui::SeparatorText("Key Concepts");
    if (ImGui::BeginTable("ConceptsTable", 2,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg,
                          ImVec2(0.0f, 0.0f))) {
        ImGui::TableSetupColumn("Term", ImGuiTableColumnFlags_WidthFixed, 120.0f);
        ImGui::TableSetupColumn("Explanation", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted("Sample");
        ImGui::TableSetColumnIndex(1);
        ImGui::TextWrapped("Raw audio waveform in BRR format (SNES compression)");

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted("Instrument");
        ImGui::TableSetColumnIndex(1);
        ImGui::TextWrapped("Configuration for how a sample is played (envelope, pitch, etc.)");

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted("Pattern");
        ImGui::TableSetColumnIndex(1);
        ImGui::TextWrapped("Grid of notes and effects for all 8 channels over time");

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted("Sequence");
        ImGui::TableSetColumnIndex(1);
        ImGui::TextWrapped("Song structure defining which patterns play in what order");

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted("Effect/Vcmd");
        ImGui::TableSetColumnIndex(1);
        ImGui::TextWrapped("Commands that modify playback (volume, pitch, vibrato, echo, etc.)");

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted("ARAM");
        ImGui::TableSetColumnIndex(1);
        ImGui::TextWrapped("Audio RAM - 64KB of memory in the SNES for all music data");

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted("Channel");
        ImGui::TableSetColumnIndex(1);
        ImGui::TextWrapped("One of 8 independent audio voices (each plays one note at a time)");

        ImGui::EndTable();
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextDisabled("For complete documentation, use Help -> Open User Guide.");
    ImGui::TextDisabled("Use the Effects and Shortcuts tabs for quick reference while composing.");
}

void QuickGuidePanel::drawEffectsReference() {
    ImGui::TextWrapped("Quick reference for tracker effects (vcmds). Use Ctrl+E to open the FX editor and edit by name.");
    ImGui::Separator();

    if (ImGui::BeginTable("EffectsTable", 4,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY,
                          ImVec2(0.0f, 0.0f))) {
        ImGui::TableSetupColumn("Hex", ImGuiTableColumnFlags_WidthFixed, 40.0f);
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, 50.0f);
        ImGui::TableSetupColumn("Params", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableSetupColumn("Description", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();

        // E0 - Instrument
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted("E0");
        ImGui::TableSetColumnIndex(1);
        ImGui::TextUnformatted("Ins");
        ImGui::TableSetColumnIndex(2);
        ImGui::TextUnformatted("XX");
        ImGui::TableSetColumnIndex(3);
        ImGui::TextWrapped("Instrument command (normally edit instruments in the Ins column or use Alt+I/Alt+R)");

        // E1 - Panning
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted("E1");
        ImGui::TableSetColumnIndex(1);
        ImGui::TextUnformatted("Pan");
        ImGui::TableSetColumnIndex(2);
        ImGui::TextUnformatted("XX");
        ImGui::TableSetColumnIndex(3);
        ImGui::TextWrapped("Set panning (00=left, 80=center, FF=right)");

        // E2 - Pan Fade
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted("E2");
        ImGui::TableSetColumnIndex(1);
        ImGui::TextUnformatted("PFa");
        ImGui::TableSetColumnIndex(2);
        ImGui::TextUnformatted("XX YY");
        ImGui::TableSetColumnIndex(3);
        ImGui::TextWrapped("Pan fade (time, target)");

        // E3 - Vibrato On
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted("E3");
        ImGui::TableSetColumnIndex(1);
        ImGui::TextUnformatted("VOn");
        ImGui::TableSetColumnIndex(2);
        ImGui::TextUnformatted("XX YY ZZ");
        ImGui::TableSetColumnIndex(3);
        ImGui::TextWrapped("Vibrato on (delay, rate, depth)");

        // E4 - Vibrato Off
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted("E4");
        ImGui::TableSetColumnIndex(1);
        ImGui::TextUnformatted("VOf");
        ImGui::TableSetColumnIndex(2);
        ImGui::TextUnformatted("-");
        ImGui::TableSetColumnIndex(3);
        ImGui::TextWrapped("Vibrato off");

        // E5 - Global Volume
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted("E5");
        ImGui::TableSetColumnIndex(1);
        ImGui::TextUnformatted("GVl");
        ImGui::TableSetColumnIndex(2);
        ImGui::TextUnformatted("XX");
        ImGui::TableSetColumnIndex(3);
        ImGui::TextWrapped("Set global volume (00-FF)");

        // E6 - Global Volume Fade
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted("E6");
        ImGui::TableSetColumnIndex(1);
        ImGui::TextUnformatted("GVF");
        ImGui::TableSetColumnIndex(2);
        ImGui::TextUnformatted("XX YY");
        ImGui::TableSetColumnIndex(3);
        ImGui::TextWrapped("Global volume fade (time, target)");

        // E7 - Tempo
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted("E7");
        ImGui::TableSetColumnIndex(1);
        ImGui::TextUnformatted("Tmp");
        ImGui::TableSetColumnIndex(2);
        ImGui::TextUnformatted("XX");
        ImGui::TableSetColumnIndex(3);
        ImGui::TextWrapped("Set tempo");

        // E8 - Tempo Fade
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted("E8");
        ImGui::TableSetColumnIndex(1);
        ImGui::TextUnformatted("TmF");
        ImGui::TableSetColumnIndex(2);
        ImGui::TextUnformatted("XX YY");
        ImGui::TableSetColumnIndex(3);
        ImGui::TextWrapped("Tempo fade (time, target)");

        // E9 - Global Transpose
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted("E9");
        ImGui::TableSetColumnIndex(1);
        ImGui::TextUnformatted("GTr");
        ImGui::TableSetColumnIndex(2);
        ImGui::TextUnformatted("XX");
        ImGui::TableSetColumnIndex(3);
        ImGui::TextWrapped("Global transpose (signed semitones)");

        // EA - Per Voice Transpose
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted("EA");
        ImGui::TableSetColumnIndex(1);
        ImGui::TextUnformatted("PTr");
        ImGui::TableSetColumnIndex(2);
        ImGui::TextUnformatted("XX");
        ImGui::TableSetColumnIndex(3);
        ImGui::TextWrapped("Per-voice transpose (signed semitones)");

        // EB - Tremolo On
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted("EB");
        ImGui::TableSetColumnIndex(1);
        ImGui::TextUnformatted("TOn");
        ImGui::TableSetColumnIndex(2);
        ImGui::TextUnformatted("XX YY ZZ");
        ImGui::TableSetColumnIndex(3);
        ImGui::TextWrapped("Tremolo on (delay, rate, depth)");

        // EC - Tremolo Off
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted("EC");
        ImGui::TableSetColumnIndex(1);
        ImGui::TextUnformatted("TOf");
        ImGui::TableSetColumnIndex(2);
        ImGui::TextUnformatted("-");
        ImGui::TableSetColumnIndex(3);
        ImGui::TextWrapped("Tremolo off");

        // ED - Volume
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted("ED");
        ImGui::TableSetColumnIndex(1);
        ImGui::TextUnformatted("Vol");
        ImGui::TableSetColumnIndex(2);
        ImGui::TextUnformatted("XX");
        ImGui::TableSetColumnIndex(3);
        ImGui::TextWrapped("Set channel volume (00-FF)");

        // EE - Volume Fade
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted("EE");
        ImGui::TableSetColumnIndex(1);
        ImGui::TextUnformatted("VFd");
        ImGui::TableSetColumnIndex(2);
        ImGui::TextUnformatted("XX YY");
        ImGui::TableSetColumnIndex(3);
        ImGui::TextWrapped("Volume fade (time, target)");

        // EF - Subroutine Call
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted("EF");
        ImGui::TableSetColumnIndex(1);
        ImGui::TextUnformatted("Cal");
        ImGui::TableSetColumnIndex(2);
        ImGui::TextUnformatted("(special)");
        ImGui::TableSetColumnIndex(3);
        ImGui::TextWrapped("Subroutine call (not directly editable)");

        // F0 - Vibrato Fade In
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted("F0");
        ImGui::TableSetColumnIndex(1);
        ImGui::TextUnformatted("Vfi");
        ImGui::TableSetColumnIndex(2);
        ImGui::TextUnformatted("XX");
        ImGui::TableSetColumnIndex(3);
        ImGui::TextWrapped("Vibrato fade in (time)");

        // F1 - Pitch Envelope To
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted("F1");
        ImGui::TableSetColumnIndex(1);
        ImGui::TextUnformatted("PEt");
        ImGui::TableSetColumnIndex(2);
        ImGui::TextUnformatted("XX YY ZZ");
        ImGui::TableSetColumnIndex(3);
        ImGui::TextWrapped("Pitch envelope to (delay, length, semitone)");

        // F2 - Pitch Envelope From
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted("F2");
        ImGui::TableSetColumnIndex(1);
        ImGui::TextUnformatted("PEf");
        ImGui::TableSetColumnIndex(2);
        ImGui::TextUnformatted("XX YY ZZ");
        ImGui::TableSetColumnIndex(3);
        ImGui::TextWrapped("Pitch envelope from (delay, length, semitone)");

        // F3 - Pitch Envelope Off
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted("F3");
        ImGui::TableSetColumnIndex(1);
        ImGui::TextUnformatted("PEo");
        ImGui::TableSetColumnIndex(2);
        ImGui::TextUnformatted("-");
        ImGui::TableSetColumnIndex(3);
        ImGui::TextWrapped("Pitch envelope off");

        // F4 - Fine Tune
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted("F4");
        ImGui::TableSetColumnIndex(1);
        ImGui::TextUnformatted("FTn");
        ImGui::TableSetColumnIndex(2);
        ImGui::TextUnformatted("XX");
        ImGui::TableSetColumnIndex(3);
        ImGui::TextWrapped("Fine tune (signed semitones)");

        // F5 - Echo On
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted("F5");
        ImGui::TableSetColumnIndex(1);
        ImGui::TextUnformatted("EOn");
        ImGui::TableSetColumnIndex(2);
        ImGui::TextUnformatted("XX YY ZZ");
        ImGui::TableSetColumnIndex(3);
        ImGui::TextWrapped("Echo on (channels, left, right)");

        // F6 - Echo Off
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted("F6");
        ImGui::TableSetColumnIndex(1);
        ImGui::TextUnformatted("EOf");
        ImGui::TableSetColumnIndex(2);
        ImGui::TextUnformatted("-");
        ImGui::TableSetColumnIndex(3);
        ImGui::TextWrapped("Echo off");

        // F7 - Echo Params
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted("F7");
        ImGui::TableSetColumnIndex(1);
        ImGui::TextUnformatted("EPr");
        ImGui::TableSetColumnIndex(2);
        ImGui::TextUnformatted("XX YY ZZ");
        ImGui::TableSetColumnIndex(3);
        ImGui::TextWrapped("Echo params (delay, feedback, FIR index)");

        // F8 - Echo Volume Fade
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted("F8");
        ImGui::TableSetColumnIndex(1);
        ImGui::TextUnformatted("EVF");
        ImGui::TableSetColumnIndex(2);
        ImGui::TextUnformatted("XX YY ZZ");
        ImGui::TableSetColumnIndex(3);
        ImGui::TextWrapped("Echo volume fade (time, left target, right target)");

        // F9 - Pitch Slide To Note
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted("F9");
        ImGui::TableSetColumnIndex(1);
        ImGui::TextUnformatted("PSt");
        ImGui::TableSetColumnIndex(2);
        ImGui::TextUnformatted("XX YY ZZ");
        ImGui::TableSetColumnIndex(3);
        ImGui::TextWrapped("Pitch slide to note (delay, length, note)");

        // FA - Percussion Base Instrument
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted("FA");
        ImGui::TableSetColumnIndex(1);
        ImGui::TextUnformatted("PIn");
        ImGui::TableSetColumnIndex(2);
        ImGui::TextUnformatted("XX");
        ImGui::TableSetColumnIndex(3);
        ImGui::TextWrapped("Percussion base instrument (index)");

        // FB - NOP
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted("FB");
        ImGui::TableSetColumnIndex(1);
        ImGui::TextUnformatted("NOP");
        ImGui::TableSetColumnIndex(2);
        ImGui::TextUnformatted("(special)");
        ImGui::TableSetColumnIndex(3);
        ImGui::TextWrapped("NOP (not directly editable)");

        // FC - Mute Channel
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted("FC");
        ImGui::TableSetColumnIndex(1);
        ImGui::TextUnformatted("MCh");
        ImGui::TableSetColumnIndex(2);
        ImGui::TextUnformatted("-");
        ImGui::TableSetColumnIndex(3);
        ImGui::TextWrapped("Mute channel");

        // FD - Fast Forward On
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted("FD");
        ImGui::TableSetColumnIndex(1);
        ImGui::TextUnformatted("FFo");
        ImGui::TableSetColumnIndex(2);
        ImGui::TextUnformatted("-");
        ImGui::TableSetColumnIndex(3);
        ImGui::TextWrapped("Fast forward on");

        // FE - Fast Forward Off
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted("FE");
        ImGui::TableSetColumnIndex(1);
        ImGui::TextUnformatted("FFf");
        ImGui::TableSetColumnIndex(2);
        ImGui::TextUnformatted("-");
        ImGui::TableSetColumnIndex(3);
        ImGui::TextWrapped("Fast forward off");

        // FF - Unused
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted("FF");
        ImGui::TableSetColumnIndex(1);
        ImGui::TextUnformatted("Unu");
        ImGui::TableSetColumnIndex(2);
        ImGui::TextUnformatted("-");
        ImGui::TableSetColumnIndex(3);
        ImGui::TextWrapped("Unused");

        ImGui::EndTable();
    }
}

void QuickGuidePanel::drawKeyboardShortcuts() {
    ImGui::TextWrapped("Global and pattern editor keyboard shortcuts.");
    ImGui::Separator();

    // Global shortcuts
    ImGui::SeparatorText("Global");
    if (ImGui::BeginTable("GlobalShortcuts", 2,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg,
                          ImVec2(0.0f, 0.0f))) {
        ImGui::TableSetupColumn("Shortcut", ImGuiTableColumnFlags_WidthFixed, 150.0f);
        ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted("Ctrl+S");
        ImGui::TableSetColumnIndex(1);
        ImGui::TextUnformatted("Save project");

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted("Ctrl+Shift+S");
        ImGui::TableSetColumnIndex(1);
        ImGui::TextUnformatted("Save project as");

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted("F5");
        ImGui::TableSetColumnIndex(1);
        ImGui::TextUnformatted("Play song from beginning");

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted("F6");
        ImGui::TableSetColumnIndex(1);
        ImGui::TextUnformatted("Play from current pattern");

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted("F8");
        ImGui::TableSetColumnIndex(1);
        ImGui::TextUnformatted("Stop playback");

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted("Space");
        ImGui::TableSetColumnIndex(1);
        ImGui::TextUnformatted("Play/stop toggle");

        ImGui::EndTable();
    }

    ImGui::Spacing();

    // Pattern editor shortcuts
    ImGui::SeparatorText("Pattern Editor");
    if (ImGui::BeginTable("PatternShortcuts", 2,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY,
                          ImVec2(0.0f, 0.0f))) {
        ImGui::TableSetupColumn("Shortcut", ImGuiTableColumnFlags_WidthFixed, 150.0f);
        ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();

        // Navigation
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.9f, 1.0f, 1.0f));
        ImGui::TextUnformatted("--- Navigation ---");
        ImGui::PopStyleColor();

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted("Arrow keys");
        ImGui::TableSetColumnIndex(1);
        ImGui::TextUnformatted("Move cursor");

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted("Tab / Shift+Tab");
        ImGui::TableSetColumnIndex(1);
        ImGui::TextUnformatted("Next/previous channel");

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted("Page Up/Down");
        ImGui::TableSetColumnIndex(1);
        ImGui::TextUnformatted("Jump 16 rows");

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted("Home / End");
        ImGui::TableSetColumnIndex(1);
        ImGui::TextUnformatted("First/last row");

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted("Shift+Arrows");
        ImGui::TableSetColumnIndex(1);
        ImGui::TextUnformatted("Extend selection");

        // Editing
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.9f, 1.0f, 1.0f));
        ImGui::TextUnformatted("--- Editing ---");
        ImGui::PopStyleColor();

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted("Piano keys (ZSXD...)");
        ImGui::TableSetColumnIndex(1);
        ImGui::TextUnformatted("Enter notes");

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted(". (period)");
        ImGui::TableSetColumnIndex(1);
        ImGui::TextUnformatted("Enter rest");

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted("Delete");
        ImGui::TableSetColumnIndex(1);
        ImGui::TextUnformatted("Clear cell/selection");

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted("Insert (note col)");
        ImGui::TableSetColumnIndex(1);
        ImGui::TextUnformatted("Insert 1 tick, shift down");

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted("Backspace (note col)");
        ImGui::TableSetColumnIndex(1);
        ImGui::TextUnformatted("Remove 1 tick, shift up");

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted("[ / ]");
        ImGui::TableSetColumnIndex(1);
        ImGui::TextUnformatted("Change octave (±1)");

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted("Ctrl+[ / Ctrl+]");
        ImGui::TableSetColumnIndex(1);
        ImGui::TextUnformatted("Change edit step (±1)");

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted("Ctrl+E");
        ImGui::TableSetColumnIndex(1);
        ImGui::TextUnformatted("Open FX editor");

        // Selection / Clipboard
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.9f, 1.0f, 1.0f));
        ImGui::TextUnformatted("--- Selection ---");
        ImGui::PopStyleColor();

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted("Ctrl+A");
        ImGui::TableSetColumnIndex(1);
        ImGui::TextUnformatted("Select all");

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted("Ctrl+Shift+A");
        ImGui::TableSetColumnIndex(1);
        ImGui::TextUnformatted("Select current channel");

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted("Ctrl+C");
        ImGui::TableSetColumnIndex(1);
        ImGui::TextUnformatted("Copy selection");

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted("Ctrl+X");
        ImGui::TableSetColumnIndex(1);
        ImGui::TextUnformatted("Cut selection");

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted("Ctrl+V");
        ImGui::TableSetColumnIndex(1);
        ImGui::TextUnformatted("Paste");

        // Transpose
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.9f, 1.0f, 1.0f));
        ImGui::TextUnformatted("--- Transpose ---");
        ImGui::PopStyleColor();

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted("Ctrl+Up/Down");
        ImGui::TableSetColumnIndex(1);
        ImGui::TextUnformatted("Transpose selection (±1 semitone)");

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted("Ctrl+Shift+Up/Down");
        ImGui::TableSetColumnIndex(1);
        ImGui::TextUnformatted("Transpose selection (±1 octave)");

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted("Ctrl+Shift+,/.");
        ImGui::TableSetColumnIndex(1);
        ImGui::TextUnformatted("Cycle instrument (prev/next)");

        // Bulk operations
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.9f, 1.0f, 1.0f));
        ImGui::TextUnformatted("--- Bulk Ops ---");
        ImGui::PopStyleColor();

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted("Ctrl+I");
        ImGui::TableSetColumnIndex(1);
        ImGui::TextUnformatted("Interpolate selection");

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted("Alt+I");
        ImGui::TableSetColumnIndex(1);
        ImGui::TextUnformatted("Set instrument on selection");

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted("Alt+V");
        ImGui::TableSetColumnIndex(1);
        ImGui::TextUnformatted("Set volume on selection");

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted("Alt+R");
        ImGui::TableSetColumnIndex(1);
        ImGui::TextUnformatted("Open song instrument remap");

        ImGui::EndTable();
    }

    ImGui::Spacing();
    ImGui::TextDisabled("Note: Many pattern shortcuts only work when the pattern editor is focused.");
}

}  // namespace ntrak::ui
