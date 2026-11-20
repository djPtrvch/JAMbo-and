// App: миниатюры страниц и стрелки страниц (порт части AppModelEdits.swift, PagesColumn.swift, SideColumns.swift,
// ветка swift JAMbo): играющие страницы (обводка), открытая, добавление, перенос, меню страницы.
#include "App.h"
#include "model/Clip.h"
#include <algorithm>

namespace jambo {

int App::viewPage() const { return viewPage_; }

void App::openPage(int p) {
    viewPage_ = std::max(0, std::min(p, int(seq().pages().size()) - 1));
    spanView_ = false;
    bump();
}

void App::addPage() {
    if (int(seq().pages().size()) >= Params::maxPages) return;
    editSeq([](Track& t) { t.addPage(); });
    viewPage_ = int(seq().pages().size()) - 1;
    bump();
}

void App::playPages(int a, int b) {
    editSeq([&](Track& t) { t.setPlayPages(a, b); });
    viewPage_ = std::min(a, b);
    spanView_ = true;
    bump();
}

void App::movePage(int from, int to) {
    editSeq([&](Track& t) { t.movePage(from, to); });
    viewPage_ = to;
    bump();
}

void App::setClipRate(int r) {
    const auto& rates = Clip::rates();
    if (channel_ != Channel::track || std::find(rates.begin(), rates.end(), r) == rates.end()) return;
    editTrack([&](Track& t) {
        if (r == 0) t.editOpen().rate.reset();
        else t.editOpen().rate = r;
    });
}

int App::clipRate() const { return channel_ == Channel::track ? seq().openClip().speed() : 0; }

void App::pageAction(PageAction action, int i) {
    switch (action) {
    case PageAction::left: editSeq([&](Track& t) { t.nudgePages(i, Track::Nudge::left); }); break;
    case PageAction::right: editSeq([&](Track& t) { t.nudgePages(i, Track::Nudge::right); }); break;
    case PageAction::up: editSeq([&](Track& t) { t.nudgePages(i, Track::Nudge::up); }); break;
    case PageAction::down: editSeq([&](Track& t) { t.nudgePages(i, Track::Nudge::down); }); break;
    case PageAction::noteUp:
    case PageAction::noteDown: {
        const int sc = project_.scale, rt = project_.root;
        const int d = action == PageAction::noteUp ? 1 : -1;
        editSeq([&](Track& t) { t.transposePages(i, d, sc, rt); });
        break;
    }
    case PageAction::duplicate: {
        const PageRange r = seq().pageRange(i);
        editSeq([&](Track& t) { t.duplicatePages(i); });
        viewPage_ = std::min(int(seq().pages().size()) - 1, r.upper + 1);
        break;
    }
    case PageAction::copy: pageClipboard_ = seq().copyPages(i); break;
    case PageAction::paste:
        if (pageClipboard_) {
            const auto clip = *pageClipboard_;
            editSeq([&](Track& t) { t.pastePages(i, clip); });
        }
        break;
    case PageAction::clear: editSeq([&](Track& t) { t.clearPages(i); }); break;
    case PageAction::del:
        // DELETE — только эта страница, и в белой обводке.
        editSeq([&](Track& t) { t.deletePages(i, true); });
        viewPage_ = std::min(viewPage_, int(seq().pages().size()) - 1);
        break;
    }
    bump();
}

/// Стрелка страниц: открытая страница (последняя, если открыта дальше конца).
void App::pageArrow(PageAction a) {
    const int last = int(seq().pages().size()) - 1;
    const int page = viewPage_ < last ? viewPage_ : last;
    pageAction(a, page < 0 ? 0 : page);
}

bool App::canDuplicate(int i) const {
    const PageRange r = seq().pageRange(i);
    return int(seq().pages().size()) + (r.upper - r.lower + 1) <= Params::maxPages;
}

bool App::canDelete(int) const { return seq().pages().size() > 1; }

std::vector<ThumbCell> App::clipThumb(int i, int k, int page) const {
    // Свой паттерн клипа: миниатюра читает его, как открытый.
    Track t = project_.tracks[size_t(i)];
    t.open = k;
    const int n = Params::stepsPerPage;
    std::vector<ThumbCell> out(static_cast<size_t>(n));
    if (page >= int(t.pages().size())) return out;
    const int len = t.pageLen(page);
    for (int s = 0; s < n; ++s) {
        const Step& st = t.pages()[size_t(page)][size_t(s)];
        out[size_t(s)] = {st.on, !st.on && t.isTail(page * n + s), s >= len};
    }
    return out;
}

std::vector<ThumbCell> App::thumb(int page) const {
    const int n = Params::stepsPerPage;
    const Track& t = seq();
    std::vector<ThumbCell> out(static_cast<size_t>(n));
    if (page >= int(t.pages().size())) return out;
    const int len = t.pageLen(page);
    for (int s = 0; s < n; ++s) {
        const Step& st = t.pages()[size_t(page)][size_t(s)];
        out[size_t(s)] = {st.on, !st.on && t.isTail(page * n + s), s >= len};
    }
    return out;
}

}  // namespace jambo
