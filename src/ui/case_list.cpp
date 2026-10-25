#include "ui/case_list.h"

namespace dupin {

CaseListWindow caseListWindow(size_t selectedCase, size_t caseCount, size_t visibleRows) {
    CaseListWindow window;
    if (caseCount == 0 || visibleRows == 0) {
        return window;
    }

    // A selection past the end is clamped rather than refused: the list is re-read after
    // every create, rename and delete, so a stale selection is a state the renderer meets,
    // and drawing the last page beats drawing nothing.
    if (selectedCase >= caseCount) {
        selectedCase = caseCount - 1;
    }

    window.first = (selectedCase / visibleRows) * visibleRows;

    const size_t remaining = caseCount - window.first;
    window.count = remaining < visibleRows ? remaining : visibleRows;
    return window;
}

} // namespace dupin
