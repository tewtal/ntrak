#include "ntrak/ui/AramUsagePanel.hpp"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

namespace ntrak::ui {
namespace {

struct AramBarSegment {
    uint32_t from = 0;  // inclusive
    uint32_t to = 0;    // exclusive
    nspc::NspcAramRegionKind kind = nspc::NspcAramRegionKind::Free;
};

const AramBarSegment* findSegmentForAddress(const std::vector<AramBarSegment>& segments, uint32_t address) {
    for (const auto& segment : segments) {
        if (address >= segment.from && address < segment.to) {
            return &segment;
        }
    }
    return nullptr;
}

int regionPriority(nspc::NspcAramRegionKind kind) {
    switch (kind) {
    case nspc::NspcAramRegionKind::Reserved:
        return 0;
    case nspc::NspcAramRegionKind::SongIndexTable:
        return 1;
    case nspc::NspcAramRegionKind::InstrumentTable:
        return 2;
    case nspc::NspcAramRegionKind::SampleDirectory:
        return 3;
    case nspc::NspcAramRegionKind::SampleData:
        return 4;
    case nspc::NspcAramRegionKind::SequenceData:
        return 5;
    case nspc::NspcAramRegionKind::PatternTable:
        return 6;
    case nspc::NspcAramRegionKind::TrackData:
        return 7;
    case nspc::NspcAramRegionKind::SubroutineData:
        return 8;
    case nspc::NspcAramRegionKind::Free:
        return 9;
    }
    return 9;
}

ImU32 regionColor(nspc::NspcAramRegionKind kind) {
    switch (kind) {
    case nspc::NspcAramRegionKind::Reserved:
        return IM_COL32(56, 118, 255, 255);
    case nspc::NspcAramRegionKind::SongIndexTable:
        return IM_COL32(93, 214, 220, 255);
    case nspc::NspcAramRegionKind::InstrumentTable:
        return IM_COL32(58, 193, 196, 255);
    case nspc::NspcAramRegionKind::SampleDirectory:
        return IM_COL32(69, 225, 164, 255);
    case nspc::NspcAramRegionKind::SampleData:
        return IM_COL32(255, 178, 56, 255);
    case nspc::NspcAramRegionKind::SequenceData:
        return IM_COL32(84, 196, 123, 255);
    case nspc::NspcAramRegionKind::PatternTable:
        return IM_COL32(143, 171, 255, 255);
    case nspc::NspcAramRegionKind::TrackData:
        return IM_COL32(170, 224, 112, 255);
    case nspc::NspcAramRegionKind::SubroutineData:
        return IM_COL32(255, 129, 129, 255);
    case nspc::NspcAramRegionKind::Free:
        return IM_COL32(42, 44, 48, 255);
    }
    return IM_COL32(42, 44, 48, 255);
}

const char* regionTypeLabel(nspc::NspcAramRegionKind kind) {
    switch (kind) {
    case nspc::NspcAramRegionKind::Reserved:
        return "Reserved";
    case nspc::NspcAramRegionKind::SongIndexTable:
        return "SongIdx";
    case nspc::NspcAramRegionKind::InstrumentTable:
        return "InstTbl";
    case nspc::NspcAramRegionKind::SampleDirectory:
        return "SmplTbl";
    case nspc::NspcAramRegionKind::SampleData:
        return "Sample";
    case nspc::NspcAramRegionKind::SequenceData:
        return "Seq";
    case nspc::NspcAramRegionKind::PatternTable:
        return "Pattern";
    case nspc::NspcAramRegionKind::TrackData:
        return "Track";
    case nspc::NspcAramRegionKind::SubroutineData:
        return "Sub";
    case nspc::NspcAramRegionKind::Free:
        return "Free";
    }
    return "Other";
}

std::vector<AramBarSegment> buildAddressSegments(const nspc::NspcAramUsage& usage) {
    constexpr uint32_t kMaxBytes = nspc::NspcAramUsage::kTotalAramBytes;
    const uint32_t totalBytes = std::min<uint32_t>(usage.totalBytes, kMaxBytes);
    if (totalBytes == 0) {
        return {};
    }

    std::array<nspc::NspcAramRegionKind, kMaxBytes> ownership{};
    ownership.fill(nspc::NspcAramRegionKind::Free);

    std::vector<const nspc::NspcAramRegion*> orderedRegions;
    orderedRegions.reserve(usage.regions.size());
    for (const auto& region : usage.regions) {
        orderedRegions.push_back(&region);
    }

    std::stable_sort(orderedRegions.begin(), orderedRegions.end(), [](const nspc::NspcAramRegion* lhs,
                                                                      const nspc::NspcAramRegion* rhs) {
        return regionPriority(lhs->kind) < regionPriority(rhs->kind);
    });

    for (const auto* region : orderedRegions) {
        const uint32_t from = std::min<uint32_t>(region->from, totalBytes);
        const uint32_t to = std::min<uint32_t>(region->to, totalBytes);
        for (uint32_t addr = from; addr < to; ++addr) {
            if (ownership[addr] == nspc::NspcAramRegionKind::Free) {
                ownership[addr] = region->kind;
            }
        }
    }

    std::vector<AramBarSegment> segments;
    segments.reserve(64);

    uint32_t segmentStart = 0;
    nspc::NspcAramRegionKind kind = ownership[0];

    for (uint32_t addr = 1; addr < totalBytes; ++addr) {
        if (ownership[addr] == kind) {
            continue;
        }
        segments.push_back(AramBarSegment{
            .from = segmentStart,
            .to = addr,
            .kind = kind,
        });
        segmentStart = addr;
        kind = ownership[addr];
    }

    segments.push_back(AramBarSegment{
        .from = segmentStart,
        .to = totalBytes,
        .kind = kind,
    });

    return segments;
}

void drawUsageBar(const nspc::NspcAramUsage& usage) {
    constexpr uint32_t kMaxBytes = nspc::NspcAramUsage::kTotalAramBytes;
    const uint32_t totalBytes = std::min<uint32_t>(usage.totalBytes, kMaxBytes);

    const float width = ImGui::GetContentRegionAvail().x;
    const float height = 18.0f;
    if (width <= 0.0f || totalBytes == 0) {
        ImGui::Dummy(ImVec2(0.0f, height));
        return;
    }

    const auto segments = buildAddressSegments(usage);

    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    const ImVec2 p1 = ImVec2(p0.x + width, p0.y + height);
    ImDrawList* draw = ImGui::GetWindowDrawList();

    draw->AddRectFilled(p0, p1, IM_COL32(20, 20, 22, 255));

    constexpr uint32_t kTickStep = 0x2000;
    for (uint32_t addr = kTickStep; addr < totalBytes; addr += kTickStep) {
        const float x = p0.x + width * (static_cast<float>(addr) / static_cast<float>(totalBytes));
        draw->AddLine(ImVec2(x, p0.y), ImVec2(x, p1.y), IM_COL32(30, 32, 36, 255), 1.0f);
    }

    for (const auto& segment : segments) {
        if (segment.to <= segment.from) {
            continue;
        }

        const float x0 = p0.x + width * (static_cast<float>(segment.from) / static_cast<float>(totalBytes));
        const float x1 = p0.x + width * (static_cast<float>(segment.to) / static_cast<float>(totalBytes));
        draw->AddRectFilled(ImVec2(x0, p0.y), ImVec2(x1, p1.y), regionColor(segment.kind));
    }

    const ImGuiIO& io = ImGui::GetIO();
    const bool hoveredBar = io.MousePos.x >= p0.x && io.MousePos.x < p1.x && io.MousePos.y >= p0.y && io.MousePos.y < p1.y;
    if (hoveredBar && !segments.empty()) {
        float t = (io.MousePos.x - p0.x) / width;
        t = std::clamp(t, 0.0f, 0.999999f);
        const uint32_t address = static_cast<uint32_t>(t * static_cast<float>(totalBytes));

        if (const auto* hoveredSegment = findSegmentForAddress(segments, address); hoveredSegment != nullptr) {
            ImGui::BeginTooltip();
            ImGui::Text("Address: $%04X", static_cast<unsigned>(address));
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(regionColor(hoveredSegment->kind)), "%s",
                               regionTypeLabel(hoveredSegment->kind));
            ImGui::Text("Region: $%04X-$%04X (%u bytes)", static_cast<unsigned>(hoveredSegment->from),
                        static_cast<unsigned>(hoveredSegment->to - 1u),
                        static_cast<unsigned>(hoveredSegment->to - hoveredSegment->from));

            bool foundRegionLabel = false;
            for (const auto& region : usage.regions) {
                if (region.kind != hoveredSegment->kind) {
                    continue;
                }
                if (address < region.from || address >= region.to) {
                    continue;
                }
                ImGui::Separator();
                ImGui::TextUnformatted(region.label.c_str());
                foundRegionLabel = true;
                break;
            }

            if (!foundRegionLabel && hoveredSegment->kind == nspc::NspcAramRegionKind::Free) {
                ImGui::Separator();
                ImGui::TextDisabled("No allocated content");
            }

            ImGui::EndTooltip();
        }
    }

    draw->AddRect(p0, p1, IM_COL32(85, 88, 92, 255), 0.0f, 0, 1.0f);
    ImGui::Dummy(ImVec2(width, height + 4.0f));
}

}  // namespace

AramUsagePanel::AramUsagePanel(app::AppState& appState) : appState_(appState) {}

void AramUsagePanel::draw() {
    if (!appState_.project.has_value()) {
        ImGui::TextDisabled("No project loaded");
        ImGui::TextDisabled("Import an SPC to inspect ARAM layout");
        return;
    }

    auto& project = *appState_.project;
    project.refreshAramUsage();
    const auto& usage = project.aramUsage();

    const uint32_t songDataBytes = usage.sequenceBytes + usage.patternTableBytes + usage.trackBytes +
                                   usage.subroutineBytes;
    const uint32_t tableBytes = usage.songIndexBytes + usage.instrumentBytes + usage.sampleDirectoryBytes;

    drawUsageBar(usage);

    const double usedPercent =
        usage.totalBytes == 0 ? 0.0
                              : (static_cast<double>(usage.usedBytes) * 100.0 / static_cast<double>(usage.totalBytes));
    ImGui::Text("Used: %u (%.1f%%)", usage.usedBytes, usedPercent);
    ImGui::Text("Free: %u", usage.freeBytes);
    ImGui::Separator();

    ImGui::Text("Song data: %u bytes", songDataBytes);
    ImGui::Text("Tracks %u / Subs %u / Tables %u", usage.trackBytes, usage.subroutineBytes, usage.patternTableBytes);
    ImGui::Text("BRR used: %u bytes", usage.sampleDataBytes);
    ImGui::Text("Reserved: %u bytes", usage.reservedBytes);
    ImGui::Text("Index+Inst+SmpTbl: %u bytes", tableBytes);

    if (ImGui::CollapsingHeader("Region List")) {
        if (ImGui::BeginTable("AramRegionTable", 4,
                              ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY,
                              ImVec2(0.0f, 220.0f))) {
            ImGui::TableSetupColumn("From", ImGuiTableColumnFlags_WidthFixed, 58.0f);
            ImGui::TableSetupColumn("To", ImGuiTableColumnFlags_WidthFixed, 58.0f);
            ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 90.0f);
            ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableHeadersRow();

            for (const auto& region : usage.regions) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::Text("$%04X", region.from);
                ImGui::TableNextColumn();
                ImGui::Text("$%04X", static_cast<uint16_t>(region.to - 1));
                ImGui::TableNextColumn();
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(regionColor(region.kind)));
                ImGui::TextUnformatted(regionTypeLabel(region.kind));
                ImGui::PopStyleColor();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(region.label.c_str());
            }

            ImGui::EndTable();
        }
    }
}

}  // namespace ntrak::ui
