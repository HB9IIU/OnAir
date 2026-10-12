// spectrum, waterfall, histogram and constellation plots
#include "app.h"

// Defined in dvbs_ui.cpp: sets S.srMsym so the Tuner panel reflects the new SR (unchecks Auto).
void dvbsSetManualSr(App& a, double hz);

// Click on the spectrum or the waterfall to tune to that frequency (a drag still pans). The grid follows the mode: FM 0.1 MHz, DMR 12.5 kHz, else 1 kHz.
static void clickToTune(App& a, int plotId) {
    if (a.family == 55) { generalPlotMouse(a, plotId); return; }   // the general receiver: its channel marker, without retuning the radio
    if (!plt::IsPlotHovered() || a.fmScan.running) return;
    double mhz = plt::GetPlotMousePos().x;
    double lo = 0.001, hi = 1e12;   // any frequency: the radio is the limit
    double grid = 0.001;
    if (a.fmMode) grid = 0.1;
    else if (a.family == 9) grid = 0.0125;
    if (a.family >= 6) if (const ModeTuning* mt = modeTuning(a.family + 2)) { lo = mt->minMhz; hi = mt->maxMhz; }
    mhz = std::round(std::min(hi, std::max(lo, mhz)) / grid) * grid;
    if (a.family == 4 || a.family == 1 || a.family == 3) return;   // ATSC and ISDB-T channels sit on a fixed raster: use the frequency field
    const ImVec2 d = ImGui::GetMouseDragDelta(0, 0);
    if (ImGui::IsMouseReleased(0) && std::fabs(d.x) < 4 && std::fabs(d.y) < 4 && std::fabs(mhz - a.freqMhz) > 1e-6) {
        if (a.fmMode) fmTune(a, mhz); else tuneFreq(a, mhz);
        return;
    }
    if (std::fabs(mhz - a.freqMhz) > 1e-6) {
        const plt::Rect lim = plt::GetPlotLimits();   // a red line where a click would tune to
        if (!ImGui::IsMouseDown(0)) {   // the ghost of the channel box: where it will sit after the click, so the whole signal can be put inside it
            const ChanWidth cw = channelWidth(a);
            if (cw.mhz > 0) {
                const double g0 = mhz - cw.mhz / 2, g1 = mhz + cw.mhz / 2;
                if (plotId == 0) { plt::Spec gb; gb.FillColor = ImVec4(0.7f, 0.7f, 0.7f, 0.10f); plt::PlotVBand("##ghost", g0, g1, gb); }
                const double ge[2] = {g0, g1};
                plt::Spec es; es.LineColor = ImVec4(0.8f, 0.8f, 0.8f, 0.45f); es.LineWeight = 1.f;
                plt::PlotInfLines("##ghostedges", ge, 2, es);
                if (plotId == 0) {
                    char lb[48]; snprintf(lb, sizeof lb, "%.4f MHz  (%s)", mhz, widthText(cw.mhz).c_str());
                    plt::PlotText(lb, mhz, lim.Y.Max, ImVec2(0, 14 * gUi));
                }
            }
        }
        const double lx[2] = {mhz, mhz}, ly[2] = {lim.Y.Min, lim.Y.Max};
        plt::Spec ls; ls.LineColor = ImVec4(0.95f, 0.25f, 0.25f, 0.9f); ls.LineWeight = 1.2f;
        plt::PlotLine("##tunehere", lx, ly, 2, ls);
        ImGui::SetTooltip(tr("click to tune to %.4f MHz"), mhz);
    }
}

ChanWidth channelWidth(const App& a) {
    ChanWidth w;
    if (a.family == 0) {   // what the engine uses while it runs (Automatic may have just switched), else the choice
        const double act = a.engine.running() ? a.engine.activeBandwidth() : 0;
        w.mhz = act > 0 ? act : kBw[a.bwIdx].mhz;
        w.settable = w.hasAuto = true; w.manual = !a.bwAuto;
    } else if (a.isdbtMode) {   // ISDB-T: the width set, or on automatic the one the receiver works with
        w.settable = w.hasAuto = true; w.manual = a.isdbtBwMhz != 0;
        w.mhz = w.manual ? a.isdbtBwMhz : a.engine.running() && a.rx.standard == 5 ? a.rx.isdbt.bwMhz : 6;
    } else if (a.atscMode) w.mhz = 6;   // ATSC 1.0 and 3.0
    else if (a.dabMode) w.mhz = 1.536;
    else if (a.fmMode) {
        w.settable = w.hasAuto = true; w.manual = a.fmChanKhz > 0;
        w.mhz = (w.manual ? a.fmChanKhz * 1e3 : FmReceiver::kDefaultChannelWidthHz) / 1e6;
    } else if (a.family == 7) { w.mhz = a.dtmbBwMhz; w.settable = w.manual = true; }
    else if (a.family == 40 && a.engine.running() && a.rx.standard == 41 && a.rx.dvbc.symbolRate > 0) w.mhz = a.rx.dvbc.symbolRate * (1 + a.rx.dvbc.rollOff) / 1e6;
    else if (a.family == 6 && a.engine.running() && a.rx.standard == 7 && a.rx.dvbs.symbolRate > 0) w.mhz = a.rx.dvbs.symbolRate * (1 + a.rx.dvbs.rollOff) / 1e6;
    else if (a.family == 55) w.mhz = generalChannelWidthMhz(a);
    else if (const ModeTuning* mt = modeTuning(a.family + 2)) w.mhz = mt->bandwidthMhz;
    return w;
}

std::string widthText(double mhz) {
    char b[32];
    if (mhz >= 1) snprintf(b, sizeof b, "%.4g MHz", mhz); else snprintf(b, sizeof b, "%.4g kHz", mhz * 1e3);
    return b;
}

void channelWidthPopup(App& a) {
    const ChanWidth cw = channelWidth(a);
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 330 * gUi);
    ImGui::TextDisabled(tr("Channel width: %s"), widthText(cw.mhz).c_str());
    ImGui::Separator();
    if (a.family == 0) {
        if (ImGui::RadioButton(tr("Automatic"), a.bwAuto) && !a.bwAuto) setDvbBandwidth(a, -1);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("The width of the signal is measured in the spectrum and the receiver switches between 1.7, 5, 6, 7 and 8 MHz by itself."));
        if (ImGui::RadioButton(tr("Manual"), !a.bwAuto) && a.bwAuto) setDvbBandwidth(a, a.bwIdx);
        ImGui::BeginDisabled(a.bwAuto);
        ImGui::Indent(ImGui::GetFrameHeight());
        for (int i = 0; i < (int)(sizeof kBw / sizeof *kBw); i++) {
            if (i) ImGui::SameLine();
            if (ImGui::RadioButton(kBw[i].label, !a.bwAuto && i == a.bwIdx)) setDvbBandwidth(a, i);
        }
        ImGui::Unindent(ImGui::GetFrameHeight());
        ImGui::EndDisabled();
        ImGui::TextDisabled("%s", tr("DVB-T2 and DVB-T channels sit on this raster; a running radio may be opened again at another sample rate."));
    } else if (a.fmMode) {
        if (ImGui::RadioButton(tr("Automatic (220 kHz)"), a.fmChanKhz == 0) && a.fmChanKhz != 0) { a.fmChanKhz = 0; savePrefs(a); }
        if (ImGui::RadioButton(tr("Manual"), a.fmChanKhz > 0) && a.fmChanKhz == 0) { a.fmChanKhz = 180; savePrefs(a); }
        ImGui::BeginDisabled(a.fmChanKhz == 0);
        ImGui::SameLine();
        int k = a.fmChanKhz > 0 ? a.fmChanKhz : 220;
        ImGui::SetNextItemWidth(150 * gUi);
        if (ImGui::SliderInt("##fmchw", &k, (int)(FmReceiver::kMinChannelWidthHz / 1e3), (int)(FmReceiver::kMaxChannelWidthHz / 1e3), "%d kHz")) a.fmChanKhz = (k + 5) / 10 * 10;
        if (ImGui::IsItemDeactivatedAfterEdit()) savePrefs(a);
        ImGui::EndDisabled();
        ImGui::TextDisabled("%s", tr("The receiver's channel filter. Narrower keeps a strong neighbour out (150 kHz copes with one 10 dB stronger 150 kHz away); "
                            "below about 180 kHz loud passages distort a little and RDS suffers first."));
    } else if (a.isdbtMode) {
        if (ImGui::RadioButton(tr("Automatic"), a.isdbtBwMhz == 0) && a.isdbtBwMhz != 0) setIsdbtBandwidth(a, 0);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("The width is measured in the spectrum, and 6, 8 and 7 MHz are tried in turn while nothing locks."));
        for (int w : {6, 7, 8}) {
            ImGui::SameLine();
            if (ImGui::RadioButton(isdbtWidthLabel(w), a.isdbtBwMhz == w) && a.isdbtBwMhz != w) setIsdbtBandwidth(a, w);
        }
        ImGui::TextDisabled("%s", tr("6 MHz in Japan and the Americas, 8 MHz in the Maldives (ISDB-T International). The signal is the same, scaled in time by the width."));
    } else if (a.family == 7) {
        if (ImGui::RadioButton(tr("8 MHz"), a.dtmbBwMhz != 6) && a.dtmbBwMhz == 6) setDtmbBandwidth(a, 8);
        ImGui::SameLine();
        if (ImGui::RadioButton(tr("6 MHz (Cuba)"), a.dtmbBwMhz == 6) && a.dtmbBwMhz != 6) setDtmbBandwidth(a, 6);
        ImGui::TextDisabled("%s", tr("Set by the country: there is nothing to measure it from. A receiver set to the wrong width finds nothing."));
    } else if (a.family == 6) {
        ImGui::TextUnformatted(tr("Follows the symbol rate: symbol rate x (1 + roll-off)."));
        ImGui::TextDisabled("%s", tr("Choose automatic or a manual symbol rate in the tuner to change it."));
    } else {
        const char* name = tr("This mode");
        for (int i = 0; i < kNumModes; i++) if (modeSelected(a, kModes[i])) name = kModes[i].name;
        ImGui::Text(tr("%s: fixed for this mode."), name);
        ImGui::TextDisabled("%s", tr("The receiver's filters are made for the signal (or for the group of channels it watches), so a manual width would only make reception worse."));
    }
    ImGui::PopTextWrapPos();
}

// The channel band behind the trace (spectrum: a light fill and its edges) or on top of the picture (waterfall: the edges only)
static void channelBand(const App& a, bool fill) {
    if (a.family == 55) { generalChannelBand(a, fill); return; }
    const ChanWidth cw = channelWidth(a);
    if (cw.mhz <= 0) return;
    const double x0 = a.freqMhz - cw.mhz / 2, x1 = a.freqMhz + cw.mhz / 2;
    if (fill) { plt::Spec band; band.FillColor = pal::accent(pal::dev() ? 0.14f : 0.16f); plt::PlotVBand("band", x0, x1, band); }
    const double edges[2] = {x0, x1};
    plt::Spec es; es.LineColor = pal::accent(fill ? 0.40f : 0.55f); es.LineWeight = 1.f;
    plt::PlotInfLines("bandedges", edges, 2, es);
}

// "BW 7 MHz auto" in the top right corner of the spectrum: the width the band shows, and a click opens channelWidthPopup().
// Left out when it would cover yLabel, which the plot draws in its top left corner.
static float gBwBtnW = 0;   // width of the BW button as drawn this frame (0: none), for the switch beside it
static void channelWidthButton(App& a, ImVec2 plotMin, ImVec2 plotSize, const char* yLabel) {
    gBwBtnW = 0;
    const ChanWidth cw = channelWidth(a);
    if (cw.mhz <= 0) return;
    char b[64];
    snprintf(b, sizeof b, "BW %s%s###bwbtn", widthText(cw.mhz).c_str(), cw.hasAuto ? (cw.manual ? " manual" : " auto") : "");
    const ImVec2 sz = ImGui::CalcTextSize(b, nullptr, true);
    const float w = sz.x + 2 * ImGui::GetStyle().FramePadding.x, m = 4 * gUi;
    if (plotSize.x < w + 2 * m + ImGui::CalcTextSize(yLabel).x + 12 * gUi || plotSize.y < sz.y + 2 * m) return;   // no room
    gBwBtnW = w + m;
    const ImVec2 keep = ImGui::GetCursorScreenPos();
    ImGui::SetCursorScreenPos(ImVec2(plotMin.x + plotSize.x - w - m, plotMin.y + m));
    ImVec4 bg = ImGui::GetStyleColorVec4(ImGuiCol_FrameBg); bg.w = 0.85f;
    ImGui::PushStyleColor(ImGuiCol_Button, bg);
    ImGui::PushStyleColor(ImGuiCol_Text, cw.manual && cw.hasAuto ? pal::warnAmber() : ImGui::GetStyleColorVec4(ImGuiCol_Text));
    if (ImGui::SmallButton(b)) ImGui::OpenPopup("##bwpop");
    ImGui::PopStyleColor(2);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s\n\n%s", tr(cw.settable ? "Channel width: the band the receiver works with (the light band).\nClick to choose automatic or a manual width."
                                      : "Channel width: the band the receiver works with (the light band), set by the standard."),
                          tr("Zoom: mouse wheel (both axes), Ctrl+wheel (frequency), Shift+wheel (level).\nCtrl+PageUp / PageDown: step the view by one screen in frequency. Double click: reset."));
    ImGui::SetNextWindowPos(ImVec2(ImGui::GetItemRectMax().x, ImGui::GetItemRectMax().y + 2 * gUi), ImGuiCond_Appearing, ImVec2(1, 0));   // under the button, right-aligned
    if (ImGui::BeginPopup("##bwpop")) { channelWidthPopup(a); ImGui::EndPopup(); }
    ImGui::SetCursorScreenPos(keep);
}

// "Whole span" switch left of the BW button, for the modes that declare support (modeSupportsWide): decode every signal in the span
static void wideSwitch(App& a, ImVec2 plotMin, ImVec2 plotSize) {
    if (!modeSupportsWide(a.family)) return;
    const char* lb = a.wideDecode ? "Whole span: on###widebtn" : "Whole span###widebtn";
    const ImVec2 sz = ImGui::CalcTextSize(lb, nullptr, true);
    const float w = sz.x + 2 * ImGui::GetStyle().FramePadding.x, m = 4 * gUi;
    const float x = plotMin.x + plotSize.x - gBwBtnW - w - m;
    if (x < plotMin.x + 40 * gUi || plotSize.y < sz.y + 2 * m) return;   // no room
    const ImVec2 keep = ImGui::GetCursorScreenPos();
    ImGui::SetCursorScreenPos(ImVec2(x, plotMin.y + m));
    ImVec4 bg = a.wideDecode ? pal::accent(0.55f) : ImGui::GetStyleColorVec4(ImGuiCol_FrameBg); if (!a.wideDecode) bg.w = 0.85f;
    ImGui::PushStyleColor(ImGuiCol_Button, bg);
    if (ImGui::SmallButton(lb)) {
        a.wideDecode = !a.wideDecode;
        if (a.family >= 0 && a.family < 64) { if (a.wideDecode) a.wideFamMask |= 1ull << a.family; else a.wideFamMask &= ~(1ull << a.family); }
        savePrefs(a);
    }
    ImGui::PopStyleColor();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("decode every signal in the visible span, not just the tuned one"));
    ImGui::SetCursorScreenPos(keep);
}

// The carriers the whole-span decoder follows: a thin line and the frequency on the spectrum
static void wideMarkers(App& a) {
    if (!wideActive(a)) return;
    const double c = radioCenterMhz(a);
    int k = 0;
    for (const WideCarrier& w : a.rx.wide) {
        const double f = c + w.hz / 1e6, xx[2] = {f, f}, yy[2] = {a.yMin, a.yMax};
        plt::Spec s; s.LineColor = w.selected ? pal::accent(0.9f) : ImVec4(0.55f, 0.85f, 0.55f, w.active ? 0.6f : 0.3f); s.LineWeight = w.selected ? 1.5f : 1.f;
        char id[24]; snprintf(id, sizeof id, "##wc%d", k++);
        plt::PlotLine(id, xx, yy, 2, s);
    }
}

// The spectrum and the waterfall share one frequency axis: panning or zooming either moves both, and a retune brings both back to the band.
static double gXLo = 0, gXHi = 0;
static void setupFreqAxis(const App& a) {
    const double fs = (a.engine.sampleRate() > 0 ? a.engine.sampleRate() : a.tune.sampleRate) / 1e6;
    static double lastC = 0, lastFs = 0;
    const double rc = radioCenterMhz(a);   // the axis shows the radio's real centre, so a signal sits at its true frequency
    if (lastC != rc || lastFs != fs || !(gXHi > gXLo)) { gXLo = rc - fs / 2; gXHi = rc + fs / 2; }   // follow a retune (otherwise keep the user's own zoom)
    lastC = rc; lastFs = fs;
    plt::SetupAxisLimits(plt::X1, rc - fs / 2, rc + fs / 2, plt::Cond_Once);
    plt::SetupAxisLinks(plt::X1, &gXLo, &gXHi);
    plt::SetupAxisFormat(plt::X1, "%.2f");
}

// The trace at the full panel width: only the visible bins, and where they are denser than the pixels, the lowest and the highest of
// each pixel column (an envelope), so no peak is lost to a coarse picture.
static void envelope(const std::vector<double>& x, const std::vector<float>& y, double lo, double hi, int px, std::vector<double>& ox, std::vector<double>& oy) {
    ox.clear(); oy.clear();
    const size_t n = std::min(x.size(), y.size());
    if (n < 2 || !(hi > lo)) return;
    const double dx = (x[n - 1] - x[0]) / (double)(n - 1);
    if (!(dx > 0)) return;
    size_t i0 = (size_t)std::max(0.0, std::floor((lo - x[0]) / dx) - 1), i1 = (size_t)std::min((double)n - 1, std::ceil((hi - x[0]) / dx) + 1);
    if (i0 >= n) i0 = n - 1;
    if (i1 <= i0) { i1 = std::min(n - 1, i0 + 1); }
    const size_t cnt = i1 - i0 + 1;
    if (px < 1 || cnt <= (size_t)px * 2) { for (size_t i = i0; i <= i1; i++) { ox.push_back(x[i]); oy.push_back(y[i]); } return; }
    for (int c = 0; c < px; c++) {
        const size_t a0 = i0 + cnt * c / px, a1 = std::max(a0 + 1, i0 + cnt * (c + 1) / px);
        size_t imn = a0, imx = a0;
        for (size_t i = a0; i < a1 && i < n; i++) { if (y[i] < y[imn]) imn = i; if (y[i] > y[imx]) imx = i; }
        const size_t f = std::min(imn, imx), g = std::max(imn, imx);
        ox.push_back(x[f]); oy.push_back(y[f]);
        if (g != f) { ox.push_back(x[g]); oy.push_back(y[g]); }
    }
}

// Ctrl+Up / Ctrl+Down move the dB range of the spectrum by 5 dB, Ctrl+Shift+Up / Down widen or narrow it, Ctrl+Home puts it back
// to the default. Only while the spectrum is hovered, like the frequency keys below (#91).
static bool dbRangeKeys(App& a) {
    ImGuiIO& io = ImGui::GetIO();
    if (!io.KeyCtrl || io.WantTextInput) return false;
    const float step = 5;
    float lo = a.yMin, hi = a.yMax;
    if (ImGui::IsKeyPressed(ImGuiKey_UpArrow, true)) { if (io.KeyShift) { lo -= step; hi += step; } else { lo += step; hi += step; } }
    else if (ImGui::IsKeyPressed(ImGuiKey_DownArrow, true)) { if (io.KeyShift) { lo += step; hi -= step; } else { lo -= step; hi -= step; } }
    else if (ImGui::IsKeyPressed(ImGuiKey_Home)) { lo = -110; hi = -30; }
    else return false;
    lo = std::max(-160.f, std::min(20.f, lo)); hi = std::max(-160.f, std::min(20.f, hi));
    if (hi - lo < 10) return false;
    a.yMin = lo; a.yMax = hi;
    return true;
}

// Ctrl+PageUp / Ctrl+PageDown: the view steps up or down in frequency by its own width
static void freqPageKeys() {
    ImGuiIO& io = ImGui::GetIO();
    if (!io.KeyCtrl || io.WantTextInput || !(gXHi > gXLo)) return;
    const double span = gXHi - gXLo;
    if (ImGui::IsKeyPressed(ImGuiKey_PageUp, true)) { gXLo += span; gXHi += span; }
    if (ImGui::IsKeyPressed(ImGuiKey_PageDown, true)) { gXLo -= span; gXHi -= span; }
}

static void spectrumPlot(App& a, ImVec2 size, int extraFlags) {
    const char* const kYLabel = tr("power (dBFS/bin)");
    ImVec2 plotMin, plotSize;
    bool drawn = false, openCtx = false;
    static double ctxMhz = 0;
    if (plt::BeginPlot("##spec", size, plt::Flags_NoLegend | plt::Flags_NoTitle | plt::Flags_Overlay | extraFlags | (a.family == 55 && generalDragging(0) ? plt::Flags_NoPan : 0))) {
        drawn = true;
        plt::SetupAxes(nullptr, kYLabel, plt::AxisFlags_Inside, plt::AxisFlags_Inside);   // the frequencies along the bottom edge, inside, as in SDR++
        setupFreqAxis(a);
        // The dB range follows the spectrum dB fields and the keys: a change there sets the axis at once (the plot otherwise keeps the
        // range the mouse dragged or zoomed to, and that range is written back into the fields, so the two never disagree) (#91)
        static float setMin = 1, setMax = 0;
        const bool rangeEdited = a.yMin != setMin || a.yMax != setMax;
        plt::SetupAxisLimits(plt::Y1, a.yMin, a.yMax, rangeEdited ? plt::Cond_Always : plt::Cond_Once);
        if (rangeEdited) { setMin = a.yMin; setMax = a.yMax; }
        channelBand(a, true);
        plotMin = plt::GetPlotPos(); plotSize = plt::GetPlotSize();
        if (!rangeEdited) {
            const plt::Rect lim = plt::GetPlotLimits();
            if (std::fabs(lim.Y.Min - a.yMin) > 0.01 || std::fabs(lim.Y.Max - a.yMax) > 0.01) { a.yMin = setMin = (float)lim.Y.Min; a.yMax = setMax = (float)lim.Y.Max; }
        }
        if (plt::IsPlotHovered() && dbRangeKeys(a)) savePrefs(a);
        if (!a.smooth.empty()) {
            const auto x = xs(a);
            const plt::Rect vis = plt::GetPlotLimits();
            const int wpx = std::max(1, (int)plotSize.x);
            static std::vector<double> ex, ey;
            if (a.peakHold) {
                plt::Spec ps; ps.LineColor = pal::grey(0.40f); ps.LineWeight = 1.0f;
                envelope(x, a.peak, vis.X.Min, vis.X.Max, wpx, ex, ey);
                plt::PlotLine("peak", ex.data(), ey.data(), (int)ex.size(), ps);
            }
            plt::Spec ss; ss.LineColor = pal::accent(); ss.LineWeight = 1.3f;
            envelope(x, a.smooth, vis.X.Min, vis.X.Max, wpx, ex, ey);
            plt::PlotLine("spectrum", ex.data(), ey.data(), (int)ex.size(), ss);
            double cx[2] = {a.freqMhz, a.freqMhz}, cy[2] = {a.yMin, a.yMax};
            plt::Spec cs; cs.LineColor = pal::grey(0.35f);
            plt::PlotLine("centre", cx, cy, 2, cs);
            if (a.fmMode && pal::dev()) {   // the stations the scan found, as bookmarks (as in SDR++)
                const plt::Rect lim = plt::GetPlotLimits();
                int k = 0;
                for (const auto& r : a.fmScan.results) {
                    if (!r.found || r.mhz < lim.X.Min || r.mhz > lim.X.Max) continue;
                    double mx[2] = {r.mhz, r.mhz}, my[2] = {a.yMin, a.yMax};
                    plt::Spec ms; ms.LineColor = ImVec4(0.85f, 0.85f, 0.85f, 0.22f);
                    char id[16]; snprintf(id, sizeof id, "mark%d", k++);
                    plt::PlotLine(id, mx, my, 2, ms);
                    char lb[48]; snprintf(lb, sizeof lb, "%s", r.name.empty() ? "" : r.name.c_str());
                    if (lb[0]) plt::PlotText(lb, r.mhz, a.yMax, ImVec2(ImGui::CalcTextSize(lb).x * 0.5f + 4, 10 + (k % 2) * 13));
                }
            }
        }
        wideMarkers(a);
        clickToTune(a, 0);
        if (plt::IsPlotHovered() && !ImGui::IsMouseDown(0) && !ImGui::IsMouseDown(1) && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) && false) {}
        if (plt::IsPlotHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) { ctxMhz = plt::GetPlotMousePos().x; openCtx = true; }
        plt::EndPlot();
    }
    if (drawn) { channelWidthButton(a, plotMin, plotSize, kYLabel); wideSwitch(a, plotMin, plotSize); }
    // right-click: what is this signal?
    if (openCtx) ImGui::OpenPopup("##specctx");
    if (ImGui::BeginPopup("##specctx")) {
        if (ImGui::MenuItem(trw("Identify this signal"), nullptr, false, a.engine.running())) identifyAt(a, ctxMhz);
        if (!a.engine.running() && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s", tr("Start the receiver first"));
        ImGui::EndPopup();
    }
}

static void waterfallPlot(App& a, ImVec2 size, int extraFlags) {
    const int H = Waterfall::H;
    // The time scale is worked out once and kept until the plot changes size (or the first real measure of the row time is in),
    // so the labels never move while the picture scrolls under them.
    struct Scale { ImVec2 avail{-1, -1}; bool measured = false; double dt = 0; std::vector<double> pos; std::vector<std::string> lab; };
    static Scale sc;
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const bool measured = a.wf.nStamps >= 128;
    if (avail.x != sc.avail.x || avail.y != sc.avail.y || measured != sc.measured) {
        sc.avail = avail; sc.measured = measured; sc.dt = a.wf.rowDt > 0 ? a.wf.rowDt : 1.0 / 30;   // a zero would make the label step zero: an endless loop
        // a 1-2-5 step that gives a label about every three text lines
        const double secs = H * sc.dt, want = std::max(2.0, (double)avail.y / (ImGui::GetTextLineHeight() * 3));
        const double raw = secs / want, mag = std::pow(10.0, std::floor(std::log10(raw))), n = raw / mag;
        const double step = (n < 1.5 ? 1 : n < 3 ? 2 : n < 7 ? 5 : 10) * mag;
        const int dec = std::max(0, (int)std::ceil(-std::log10(step) - 1e-9));
        sc.pos.clear(); sc.lab.clear();
        for (double s = 0; s <= secs + 1e-9; s += step) {
            char b[32]; snprintf(b, sizeof b, "%.*f", dec, s > 0 ? -s : 0.0);
            sc.pos.push_back(-s / sc.dt); sc.lab.push_back(b);
        }
    }
    if (plt::BeginPlot("##wf", size, plt::Flags_NoLegend | plt::Flags_NoTitle | extraFlags | (a.family == 55 && generalDragging(1) ? plt::Flags_NoPan : 0))) {
        plt::SetupAxes(nullptr, tr("seconds ago"), plt::AxisFlags_NoTickLabels, plt::AxisFlags_Inside);   // the spectrum above shows the frequencies
        double fs = (a.engine.sampleRate() > 0 ? a.engine.sampleRate() : a.tune.sampleRate) / 1e6;
        const double rc = radioCenterMhz(a);
        double x0 = rc - fs / 2, x1 = rc + fs / 2;
        setupFreqAxis(a);
        // y is in rows, so the picture keeps its size while the measured row time wanders; the labels are the fixed scale above
        plt::SetupAxisLimits(plt::Y1, -H, 0, plt::Cond_Always);
        plt::SetupAxisUnit(plt::Y1, sc.dt);
        std::vector<const char*> labs;
        for (auto& s : sc.lab) labs.push_back(s.c_str());
        plt::SetupAxisTicks(plt::Y1, sc.pos.data(), (int)sc.pos.size(), labs.data());
        // rows arrive at the engine's ~30 Hz, the screen draws faster: slide down by the part of a row that is due since the last one,
        // so the picture moves at an even speed instead of jumping a row at a time
        const double frac = a.wf.pushT > 0 ? std::min(1.0, std::max(0.0, (glfwGetTime() - a.wf.pushT) / a.wf.rowDt)) : 0;
        int w = a.wf.writeRow;
        ImTextureID tex = a.wf.img->texture();
        plt::PlotImage("a", tex, plt::Point(x0, -(H - w) - frac), plt::Point(x1, -frac), ImVec2(0, (float)w / H), ImVec2(1, 1));
        if (w > 0)
            plt::PlotImage("b", tex, plt::Point(x0, -H - frac), plt::Point(x1, -(H - w) - frac), ImVec2(0, 0), ImVec2(1, (float)w / H));
        const float vNew = (w + 0.5f) / H;   // the gap above: the newest row, stretched
        if (frac > 0) plt::PlotImage("top", tex, plt::Point(x0, -frac), plt::Point(x1, 0), ImVec2(0, vNew), ImVec2(1, vNew));
        channelBand(a, false);
        clickToTune(a, 1);
        plt::EndPlot();
    }
}

// The spectrum on top of the waterfall as one panel (as in SDR++): no gap, the same edges and frequency axis.
// frac: the spectrum's share of the height; with drag set, the boundary line is a handle that changes it.
void spectrumWaterfall(App& a, float& frac, bool drag) {
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float w = std::max(60.f, avail.x), h = avail.y;
    const float hs = std::floor(std::min(std::max(60.f, h * frac), std::max(60.f, h - 60.f)));
    const float y = p.y + hs;
    bool hot = false;
    if (drag) {   // first, so the handle takes the mouse before the plots under it
        ImGui::SetCursorScreenPos(ImVec2(p.x, y - 3 * gUi));
        ImGui::InvisibleButton("##split", ImVec2(w, 6 * gUi));
        hot = ImGui::IsItemHovered() || ImGui::IsItemActive();
        if (hot) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
        if (ImGui::IsItemActive()) frac = std::min(0.8f, std::max(0.2f, frac + ImGui::GetIO().MouseDelta.y / std::max(1.f, h)));
        ImGui::SetCursorScreenPos(p);
    }
    freqPageKeys();
    spectrumPlot(a, ImVec2(w, hs), plt::Flags_Flush | plt::Flags_OpenBottom);
    ImGui::SetCursorScreenPos(ImVec2(p.x, y));
    waterfallPlot(a, ImVec2(w, std::max(60.f, h - hs)), plt::Flags_Flush | plt::Flags_OpenTop);
    ImGui::GetWindowDrawList()->AddLine(ImVec2(p.x, y), ImVec2(p.x + w, y), hot ? IM_COL32(200, 200, 200, 160) : IM_COL32(110, 110, 110, 110), hot ? 2.f : 1.f);
}

void batcSpectrumPlot(App& a, ImVec2 size) {
    auto frame = a.batc.latest();
    // Smooth the incoming data (exponential moving average, same pattern as the local spectrum)
    if (!frame.empty()) {
        const size_t n = frame.size();
        if (a.batcSmooth.size() != n) a.batcSmooth.assign(n, 0.f);
        for (size_t i = 0; i < n; i++) {
            float v = (float)((frame[i] / 65535.0 - BatcSpectrum::DISPLAY_FLOOR) / (BatcSpectrum::DISPLAY_CEILING - BatcSpectrum::DISPLAY_FLOOR));
            v = std::max(0.f, std::min(1.f, v));
            a.batcSmooth[i] += 0.35f * (v - a.batcSmooth[i]);
        }
    }
    // LNB LO control — right-aligned, on the same line as the plot label area
    {
        const float fieldW = 90 * gUi;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - fieldW);
        ImGui::SetNextItemWidth(fieldW);
        ImGui::DragFloat("##lnblo", &a.batcLnbMhz, 1.0f, 1000.0f, 20000.0f, "LNB %.0f MHz");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("LNB local oscillator (MHz)\nIF = satellite \xe2\x88\x92 LNB LO\nDrag or double-click to edit\n9750 = Ku low band\n10600 = Ku high band");
        // account for the one line we just drew so the plot fills the remaining height
        size.y -= ImGui::GetItemRectSize().y + ImGui::GetStyle().ItemSpacing.y;
    }
    if (plt::BeginPlot("##batc", size, plt::Flags_NoLegend | plt::Flags_NoTitle)) {
        plt::SetupAxes("frequency (MHz)", nullptr);
        plt::SetupAxisLimits(plt::X1, BatcSpectrum::START_MHZ, BatcSpectrum::END_MHZ, plt::Cond_Once);
        plt::SetupAxisLimits(plt::Y1, 0, 1.15, plt::Cond_Always);
        plt::SetupAxisFormat(plt::X1, "%.1f");
        if (!a.batcSmooth.empty()) {
            const size_t n = a.batcSmooth.size();
            std::vector<double> x(n), y(n);
            for (size_t i = 0; i < n; i++) {
                x[i] = BatcSpectrum::START_MHZ + (double)i / (n - 1) * (BatcSpectrum::END_MHZ - BatcSpectrum::START_MHZ);
                y[i] = a.batcSmooth[i];
            }
            plt::Spec ss; ss.LineColor = ImVec4(1.0f, 0.83f, 0.23f, 1.0f); ss.LineWeight = 1.3f;
            plt::PlotLine("QO-100 WB", x.data(), y.data(), (int)n, ss);
            plt::Spec fs; fs.FillColor = ImVec4(1.0f, 0.83f, 0.23f, 0.12f); fs.LineColor = ImVec4(0, 0, 0, 0);
            plt::PlotShaded("fill", x.data(), y.data(), (int)n, 0.0, fs);
        }
        if (!a.batc.isConnected()) {
            plt::PlotText("Connecting to BATC...", (BatcSpectrum::START_MHZ + BatcSpectrum::END_MHZ) / 2, 0.5);
        } else if (frame.empty() && a.batcSmooth.empty()) {
            plt::PlotText("Waiting for BATC spectrum data...", (BatcSpectrum::START_MHZ + BatcSpectrum::END_MHZ) / 2, 0.5);
        }
        // Click-to-tune: hover shows a red line + tooltip with detected SR, click tunes
        if (plt::IsPlotHovered() && !frame.empty()) {
            const double satMhz = plt::GetPlotMousePos().x;
            if (satMhz >= BatcSpectrum::START_MHZ && satMhz <= BatcSpectrum::END_MHZ) {
                // Detect signal under cursor: find noise floor, peak, -3 dB bandwidth, snap to standard SR
                static const int kSrs[] = {25,33,50,66,125,250,333,500,1000,1500,2000};
                const size_t N = frame.size();
                const double mhzPerBin = (BatcSpectrum::END_MHZ - BatcSpectrum::START_MHZ) / (N - 1);
                // smooth (7-bin average, same as QOCast)
                std::vector<float> sm(N);
                for (size_t i = 0; i < N; i++) {
                    float s = 0; int c = 0;
                    for (int k = -3; k <= 3; k++) { size_t j = (size_t)std::max(0,(int)i+k); if (j < N) { s += frame[j]; c++; } }
                    sm[i] = s / c;
                }
                // noise floor = median
                std::vector<float> sorted(sm); std::sort(sorted.begin(), sorted.end());
                const float noise = sorted[N / 2];
                // peak near cursor (±0.15 MHz)
                const int cBin = (int)std::round((satMhz - BatcSpectrum::START_MHZ) / mhzPerBin);
                const int rad  = (int)std::round(0.15 / mhzPerBin);
                int peak = std::max(0, cBin - rad);
                for (int i = peak; i <= std::min((int)N-1, cBin+rad); i++) if (sm[i] > sm[peak]) peak = i;
                // -3 dB bandwidth
                int srKsps = 0;
                double detSatMhz = satMhz;
                if (sm[peak] - noise > 2000) {
                    const float half = noise + (sm[peak] - noise) * 0.5f;
                    int L = peak, R = peak;
                    while (L > 0 && sm[L-1] > half) L--;
                    while (R < (int)N-1 && sm[R+1] > half) R++;
                    detSatMhz = BatcSpectrum::START_MHZ + (L + R) * 0.5 * mhzPerBin;
                    // snap to nearest beacon or standard SR
                    if (std::fabs(detSatMhz - 10491.5) < 0.3) { detSatMhz = 10491.5; srKsps = 1500; }
                    else {
                        const double measKsps = (R - L + 1) * mhzPerBin * 1000.0;
                        int best = kSrs[0];
                        for (int sr : kSrs) if (std::fabs(std::log((double)sr/measKsps)) < std::fabs(std::log((double)best/measKsps))) best = sr;
                        srKsps = best;
                    }
                }
                // red hover line at detected centre (or cursor if no signal)
                const double lineMhz = srKsps > 0 ? detSatMhz : satMhz;
                const plt::Rect lim = plt::GetPlotLimits();
                const double lx[2] = {lineMhz, lineMhz}, ly[2] = {lim.Y.Min, lim.Y.Max};
                plt::Spec ls; ls.LineColor = ImVec4(0.95f, 0.25f, 0.25f, 0.9f); ls.LineWeight = 1.2f;
                plt::PlotLine("##tune", lx, ly, 2, ls);
                // tooltip
                const double ifMhz = lineMhz - a.batcLnbMhz;
                char tip[96];
                if (srKsps > 0)
                    snprintf(tip, sizeof tip, "Tune to %.3f MHz  %d kS/s\n(IF %.3f MHz)", lineMhz, srKsps, ifMhz);
                else
                    snprintf(tip, sizeof tip, "Tune to %.3f MHz\n(IF %.3f MHz)", lineMhz, ifMhz);
                ImGui::SetTooltip("%s", tip);
                // click to tune frequency + SR
                const ImVec2 d = ImGui::GetMouseDragDelta(0, 0);
                if (ImGui::IsMouseReleased(0) && std::fabs(d.x) < 4 && std::fabs(d.y) < 4) {
                    tuneFreq(a, ifMhz);
                    if (srKsps > 0 && a.family == 6)   // family 6 = DVB-S/S2
                        dvbsSetManualSr(a, srKsps * 1000.0);
                    a.engine.player().select(-1);       // stop stale video
                    a.autoPlayAfter = glfwGetTime() + 1.0; // auto-play 1s after retune (lets tsSnapshot refresh)
                }
            }
        }
        plt::EndPlot();
    }
}

void histogramPlot(App& a, ImVec2 size) {
    if (plt::BeginPlot("##hist", size, plt::Flags_NoLegend | plt::Flags_NoTitle)) {
        plt::SetupAxes("|sample| (fraction of full scale)", "count");
        plt::SetupAxisScale(plt::Y1, plt::Scale_Log10);
        plt::SetupAxisLimits(plt::X1, 0, 1, plt::Cond_Once);
        float h[64], x[64];
        for (int i = 0; i < 64; i++) { h[i] = (float)std::max<uint32_t>(1, a.spec.stats.hist[i]); x[i] = (i + 0.5f) / 64; }
        plt::Spec bs; bs.FillColor = pal::accent(0.8f);
        plt::PlotBars("adc", x, h, 64, 1.0 / 64, bs);
        plt::EndPlot();
    }
}

// Decoded-cell views of the data constellation: density heat map and per-point clusters
void constDensityPlot(App& a, ImVec2 sz) {
    const App::ConstStats& c = a.cst;
    if (plt::BeginPlot("##c2d", sz, plt::Flags_NoLegend | plt::Flags_NoTitle | plt::Flags_Equal | plt::Flags_NoMouseText)) {
        plt::SetupAxes(nullptr, nullptr, plt::AxisFlags_NoTickLabels, plt::AxisFlags_NoTickLabels);
        plt::SetupAxisLimits(plt::X1, -1.4, 1.4, plt::Cond_Always);
        plt::SetupAxisLimits(plt::Y1, -1.4, 1.4, plt::Cond_Always);
        const int G = App::ConstStats::G;
        if ((int)c.grid.size() == G * G) {
            std::vector<float> v(c.grid.size());
            float mx = 0;
            for (size_t i = 0; i < v.size(); i++) { v[i] = std::sqrt(c.grid[i]); mx = std::max(mx, v[i]); }   // square root: faint areas stay visible
            static int cm = -1;
            if (cm < 0) {   // black -> accent -> white, instead of the rainbow-like "hot" map
                const ImVec4 cols[4] = {ImVec4(0.02f, 0.03f, 0.05f, 1), ImVec4(0.10f, 0.28f, 0.45f, 1), pal::accent(), ImVec4(0.95f, 0.98f, 1.f, 1)};
                cm = plt::AddColormap("onair", cols, 4);
            }
            plt::PushColormap(cm);
            plt::PlotHeatmap("density", v.data(), G, G, 0, std::max(1.f, mx * 0.85f), nullptr, plt::Point(-1.4, -1.4), plt::Point(1.4, 1.4));
            plt::PopColormap();
        }
        const int M = 2 * (a.rx.plpFec.mod + 1);
        std::vector<cf32> grid;
        for (unsigned l = 0; l < (1u << M); l++) grid.push_back(qamPoint(a.rx.plpFec.mod, false, l));
        plt::Spec gs; gs.Marker = plt::Marker_Cross; gs.MarkerSize = 4.f; gs.Stride = sizeof(cf32);
        gs.MarkerFillColor = gs.MarkerLineColor = gs.LineColor = ImVec4(1, 1, 1, 0.55f);
        const float* g = reinterpret_cast<const float*>(grid.data());
        plt::PlotScatter("ideal", g, g + 1, (int)grid.size(), gs);
        plt::EndPlot();
    }
}

void constClusterPlot(App& a, ImVec2 sz) {
    const App::ConstStats& c = a.cst;
    const int mod = a.rx.plpFec.mod;
    static const float kDmin[4] = {1.4142f, 0.6325f, 0.3086f, 0.1534f};
    const float half = 0.5f * kDmin[mod];
    if (plt::BeginPlot("##c2k", sz, plt::Flags_NoLegend | plt::Flags_NoTitle | plt::Flags_Equal | plt::Flags_NoMouseText)) {
        plt::SetupAxes(nullptr, nullptr, plt::AxisFlags_NoTickLabels, plt::AxisFlags_NoTickLabels);
        plt::SetupAxisLimits(plt::X1, -1.4, 1.4, plt::Cond_Always);
        plt::SetupAxisLimits(plt::Y1, -1.4, 1.4, plt::Cond_Always);
        int hover = -1;
        double meanRatio = 0; int nr = 0;
        for (auto& p : c.pts) if (p.n >= 4) { meanRatio += std::sqrt(p.e2 / (2 * p.n)); nr++; }
        meanRatio = nr ? meanRatio / nr : 1;
        const plt::Point mp = plt::GetPlotMousePos();
        double bestD = 1e9;
        for (size_t l = 0; l < c.pts.size(); l++) {
            const App::ConstStats::Pt& p = c.pts[l];
            const cf32 tx = qamPoint(mod, false, (unsigned)l);
            const double d = std::hypot(mp.x - tx.real(), mp.y - tx.imag());
            if (plt::IsPlotHovered() && d < bestD && d < half * 1.2) { bestD = d; hover = (int)l; }
            if (p.n < 4) continue;
            const double sg = std::sqrt(p.e2 / (2 * p.n));        // standard deviation per axis
            // colour relative to the average ring: green = tighter than average, red = looser (a coded signal is decoded at
            // noise levels where all rings overlap, so an absolute scale would show everything red)
            const float t = (float)std::min(1.0, std::max(0.0, 0.5 + (sg / meanRatio - 1.0) * 4.0));
            const ImVec4 col(0.25f + 0.7f * t, 0.85f - 0.5f * t, 0.45f - 0.15f * t, 0.9f);
            const double cx = tx.real() + p.ei / p.n, cy = tx.imag() + p.eq / p.n;
            float xs[25], ys[25];
            for (int k = 0; k < 25; k++) { const double th = k * 2 * M_PI / 24; xs[k] = (float)(cx + sg * std::cos(th)); ys[k] = (float)(cy + sg * std::sin(th)); }
            plt::Spec ls; ls.LineColor = col; ls.LineWeight = (int)l == hover ? 2.5f : 1.3f;
            plt::PlotLine("##ring", xs, ys, 25, ls);
            plt::Spec ds; ds.Marker = plt::Marker_Circle; ds.MarkerSize = 2.f; ds.MarkerFillColor = ds.MarkerLineColor = ds.LineColor = col;
            const float px = (float)cx, py = (float)cy;
            plt::PlotScatter("##centre", &px, &py, 1, ds);
        }
        {
            const int M = 2 * (mod + 1);
            std::vector<cf32> grid;
            for (unsigned l = 0; l < (1u << M); l++) grid.push_back(qamPoint(mod, false, l));
            plt::Spec gs; gs.Marker = plt::Marker_Cross; gs.MarkerSize = 4.f; gs.Stride = sizeof(cf32);
            gs.MarkerFillColor = gs.MarkerLineColor = gs.LineColor = ImVec4(1, 1, 1, 0.8f);
            const float* g = reinterpret_cast<const float*>(grid.data());
            plt::PlotScatter("ideal", g, g + 1, (int)grid.size(), gs);
        }
        if (hover >= 0 && c.pts[hover].n >= 4) {
            const App::ConstStats::Pt& p = c.pts[hover];
            const int M = 2 * (mod + 1);
            char bits[16];
            for (int b = 0; b < M; b++) bits[b] = ((hover >> (M - 1 - b)) & 1) ? '1' : '0';
            bits[M] = 0;
            ImGui::BeginTooltip();
            ImGui::Text(tr("point %s"), bits);
            ImGui::Text(tr("sigma %.3f per axis (%.0f%% of the half-distance to a neighbour)"), std::sqrt(p.e2 / (2 * p.n)), 100 * std::sqrt(p.e2 / (2 * p.n)) / half);
            ImGui::Text(tr("offset %+.3f %+.3f"), p.ei / p.n, p.eq / p.n);
            ImGui::Text(tr("EVM %.1f dB"), 10 * std::log10(std::max(1e-9, p.e2 / p.n)));
            ImGui::EndTooltip();
        }
        plt::EndPlot();
    }
}

