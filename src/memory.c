#include "memory.h"
#include "common.h"
#include "error.h"
#include "vm.h"

void *reallocate(void *old_ptr, size_t old_size, size_t new_size) {
    vm.allocated += new_size - old_size;

#ifdef DEBUG_STRESS_GC
    if (new_size > old_size) collect_garbage();
#else
    if (vm.allocated >= vm.next_gc) collect_garbage();
#endif

    if (new_size == 0) {
        free(old_ptr);
        return NULL;
    }

    void *new_ptr = realloc(old_ptr, new_size);
    if (new_ptr == NULL) OUT_OF_MEMORY();

    return new_ptr;
}

void mark_object(Object *object) {
    if (object == NULL || object->is_marked) return;
    object->is_marked = true;

#ifdef DEBUG_LOG_GC
    printf("%p mark ", (void *) object);
    print_object(object);
    printf("\n");
#endif

    switch (object->type) {
        case OBJ_STRING:
        case OBJ_NATIVE: return;
        default:         break;
    }

    if (vm.grey_length >= vm.grey_capacity) {
        vm.grey_capacity = GREY_GROW_CAPACITY(vm.grey_capacity);
        vm.grey_objects = realloc(vm.grey_objects, sizeof(*vm.grey_objects) * vm.grey_capacity);
        if (vm.grey_objects == NULL) OUT_OF_MEMORY();
    }
    vm.grey_objects[vm.grey_length++] = object;
}

void mark_value(Value *value) {
    if (value->type == VAL_OBJECT) mark_object(value->as.object);
}

static void mark_roots(void) {
    for (Value *value = vm.stack; value < vm.stack_top; value++) {
        mark_value(value);
    }

    for (uint32_t i = 0; i < vm.globals.capacity; i++) {
        Entry *entry = &vm.globals.entries[i];
        if (entry->key == NULL) continue;

        mark_object((Object *) entry->key);
        mark_value(&entry->value);
    }

    for (CallFrame *frame = vm.frames; frame <= vm.frame; frame++) {
        mark_object((Object *) frame->closure);
    }

    for (ObjUpvalue *upvalue = vm.open_upvalues; upvalue != NULL; upvalue = upvalue->next) {
        mark_object((Object *) upvalue);
    }
}

static void trace_object(Object *object) {
#ifdef DEBUG_LOG_GC
    printf("%p trace ", (void *) object);
    print_object(object);
    printf("\n");
#endif

    switch (object->type) {
        case OBJ_FUNCTION: {
            ObjFunction *function = (ObjFunction *) object;
            mark_object((Object *) function->name);
            for (uint32_t i = 0; i < function->chunk.constants.length; i++) {
                mark_value(&function->chunk.constants.values[i]);
            }
        } break;
        case OBJ_UPVALUE: mark_value(((ObjUpvalue *) object)->location); break;
        case OBJ_CLOSURE: {
            ObjClosure *closure = (ObjClosure *) object;
            mark_object((Object *) closure->function);
            for (uint32_t i = 0; i < closure->upvalues_length; i++) {
                mark_object((Object *) closure->upvalues[i]);
            }
        } break;
        default: UNREACHABLE();
    }
}

static void delete_unmarked_strings(void) {
    for (uint32_t i = 0; i < vm.strings.capacity; i++) {
        Entry *entry = &vm.strings.entries[i];
        if (entry->key != NULL && !entry->key->object.is_marked) {
            hashmap_delete(&vm.strings, entry->key);
        }
    }
}

static void sweep(void) {
    Object *prev = NULL, *current = vm.objects;
    while (current != NULL) {
        if (current->is_marked) {
            current->is_marked = false;
            prev = current;
            current = current->next;
            continue;
        }

        Object *unreachable = current;
#ifdef DEBUG_LOG_GC
        printf("%p free ", (void *) unreachable);
        print_object(unreachable);
        printf("\n");
#endif
        current = current->next;

        if (prev == NULL) {
            vm.objects = current;
        } else {
            prev->next = current;
        }
        free_object(unreachable);
    }
}

void collect_garbage(void) {
#ifdef DEBUG_LOG_GC
    printf("--- gc begin\n");
    size_t before = vm.allocated;
#endif

    mark_compiler_roots();
    mark_roots();

    while (vm.grey_length > 0) {
        Object *object = vm.grey_objects[--vm.grey_length];
        trace_object(object);
    }
    delete_unmarked_strings();
    sweep();

    vm.next_gc = vm.allocated * GC_GROW_FACTOR;

#ifdef DEBUG_LOG_GC
    printf("--- gc end\n");
    printf("    collected %zu bytes\n", vm.allocated - before);
    printf("    next at %zu bytes\n", vm.next_gc);
#endif
}
