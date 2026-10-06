/* Staged inside the pinned editor so restored sidebars use its parser/filter
 * helpers. No process pointers, animation callbacks or parser-owned lists are
 * accepted from a checkpoint. Prepare everything before changing the editor. */
_Static_assert(TM_CODE_DRAG == TEXT_DRAG_CODE && TM_CODE_FIND == TEXT_FIND_MODE &&
    TM_CODE_GOTO == TEXT_GOTO_MODE && TM_CODE_BOOKMARK == TEXT_BOOKMARK_MODE &&
    TM_CODE_OUTLINE == TEXT_OUTLINE_MODE && TM_CODE_REPLACE == TEXT_REPLACE_MODE &&
    TM_CODE_EDIT == TEXT_EDIT_MODE, "Pinned code modes changed");

static bool tm_code_sidebar_append(tic_outline_item** items, s32* size,
    s32* capacity, tic_outline_item item)
{
    if(*size == *capacity)
    {
        s32 next = MIN(*capacity ? *capacity * 2 : 16, TIC_CODE_SIZE);
        if(next <= *capacity) return false;
        void* grown = realloc(*items, (size_t)next * sizeof **items);
        if(!grown) return false;
        *items = grown; *capacity = next;
    }
    (*items)[(*size)++] = item;
    return true;
}

bool tm_code_restore_view_mode(Code* code, const tm_studio_code_view* view)
{
    size_t length = strnlen(code->src, TIC_CODE_SIZE);
    if(!tm_studio_code_view_valid(view, length)) return false;
    if(view->mode == TM_CODE_GOTO && view->jump_line > getLinesCount(code)) return false;
    tic_outline_item* items = NULL;
    s32 size = 0, capacity = 0;
    if(view->mode == TM_CODE_BOOKMARK)
    {
        for(size_t offset = 0; offset < length; ++offset)
            if(code->state[offset].bookmark && !tm_code_sidebar_append(&items, &size,
                &capacity, (tic_outline_item){.pos = code->src + offset, .size = getLineSize(code->src + offset)})) goto fail;
    }
    else if(view->mode == TM_CODE_OUTLINE)
    {
        const tic_script* script = tic_get_script(code->tic);
        s32 count = 0;
        const tic_outline_item* source = script && script->getOutline ? script->getOutline(code->src, &count) : NULL;
        if(count < 0 || count > TIC_CODE_SIZE || (count && !source)) goto fail;
        for(s32 i = 0; i < count; ++i)
        {
            tic_outline_item item = source[i];
            uintptr_t start = (uintptr_t)code->src, position = (uintptr_t)item.pos;
            if(position < start || position - start > length || item.size < 0 ||
                (size_t)item.size > length - (position - start)) goto fail;
            if(code->state[position - start].syntax == SyntaxType_COMMENT ||
                (*view->popup_text && !isFilterMatch(item.pos, item.size, view->popup_text))) continue;
            if(!tm_code_sidebar_append(&items, &size, &capacity, item)) goto fail;
        }
        if(size) qsort(items, size, sizeof *items, funcCompare);
    }
    if(view->sidebar_count != size || view->sidebar_scroll > MAX(0, size - TEXT_BUFFER_HEIGHT)) goto fail;
    // All potentially failing work is complete. Swap the private list and
    // rebind every saved offset to this worker's source/popup buffers.
    free(code->sidebar.items); code->sidebar.items = items;
    code->sidebar.size = size; code->sidebar.index = view->sidebar_index; code->sidebar.scroll = view->sidebar_scroll;
    code->mode = view->mode;
    memcpy(code->popup.text, view->popup_text, sizeof code->popup.text);
    code->popup.prevPos = view->previous_cursor < 0 ? NULL : code->src + view->previous_cursor;
    code->popup.prevSel = view->previous_selection < 0 ? NULL : code->src + view->previous_selection;
    code->popup.offset = view->replace_offset < 0 ? NULL : code->popup.text + view->replace_offset;
    code->jump.line = view->jump_line;
    code->anim.movie = view->animation == TM_CODE_SHOW ? &code->anim.show : view->animation == TM_CODE_HIDE ? &code->anim.hide : &code->anim.idle;
    code->anim.movie->tick = view->animation_tick;
    code->anim.pos = view->popup_y; code->anim.sidebar = view->sidebar_x;
    code->cursor.mouseDownPosition = NULL; code->scroll.active = false;
    return true;
fail:
    free(items);
    return false;
}
