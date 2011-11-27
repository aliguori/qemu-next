#include <Python.h>
#include "qemu-common.h"
#include "hw/hw.h"
#include "pyembed.h"
#include <pyglib.h>

typedef struct OpaqueData
{
    void *read;
    void *write;
} OpaqueData;

static OpaqueData opaque_data[64 * 1024];

static void pyembed_ioport_write_trampoline(void *opaque, uint32_t address, int size, uint32_t value)
{
    OpaqueData *data = opaque;
    PyObject *callback = data->write;
    PyObject *arglist, *result;

    arglist = Py_BuildValue("(IiI)", address, size, value);
    result = PyObject_CallObject(callback, arglist);

    Py_DECREF(arglist);
    Py_DECREF(result);
}

static void pyembed_ioport_write_trampoline1(void *opaque, uint32_t address, uint32_t value)
{
    return pyembed_ioport_write_trampoline(opaque, address, 1, value);
}

static void pyembed_ioport_write_trampoline2(void *opaque, uint32_t address, uint32_t value)
{
    return pyembed_ioport_write_trampoline(opaque, address, 2, value);
}

static void pyembed_ioport_write_trampoline4(void *opaque, uint32_t address, uint32_t value)
{
    return pyembed_ioport_write_trampoline(opaque, address, 4, value);
}

static PyObject *pyembed_register_ioport_write(PyObject *self, PyObject *args)
{
    pio_addr_t addr;
    int length;
    PyObject *callback;
    int ret;
    
    if (!PyArg_ParseTuple(args, "iiO", &addr, &length, &callback)) {
        return NULL;
    }

    if (!PyCallable_Check(callback)) {
        PyErr_SetString(PyExc_TypeError, "parameter must be callable");
        return NULL;
    }

    Py_XINCREF(callback);

    opaque_data[addr].write = callback;

    ret = register_ioport_write(addr, length, 1,
                                pyembed_ioport_write_trampoline1, &opaque_data[addr]);
    ret = register_ioport_write(addr, length, 2,
                                pyembed_ioport_write_trampoline2, &opaque_data[addr]);
    ret = register_ioport_write(addr, length, 4,
                                pyembed_ioport_write_trampoline4, &opaque_data[addr]);

    if (ret == -1) {
        Py_XDECREF(callback);
    }

    return Py_BuildValue("i", ret);
}

static uint32_t pyembed_ioport_read_trampoline(void *opaque, uint32_t address, int size)
{
    OpaqueData *data = opaque;
    PyObject *callback = data->read;
    PyObject *arglist, *result;
    uint32_t ret = -1U;

    arglist = Py_BuildValue("(Ii)", address, size);

    result = PyObject_CallObject(callback, arglist);
    if (!PyInt_Check(result)) {
        PyErr_SetString(PyExc_TypeError, "return value must be an integer");
        goto out;
    }

    ret = PyInt_AsUnsignedLongMask(result);

out:
    Py_DECREF(result);
    Py_DECREF(arglist);
    return ret;
}

static uint32_t pyembed_ioport_read_trampoline1(void *opaque, uint32_t address)
{
    return pyembed_ioport_read_trampoline(opaque, address, 1);
}

static uint32_t pyembed_ioport_read_trampoline2(void *opaque, uint32_t address)
{
    return pyembed_ioport_read_trampoline(opaque, address, 2);
}

static uint32_t pyembed_ioport_read_trampoline4(void *opaque, uint32_t address)
{
    return pyembed_ioport_read_trampoline(opaque, address, 4);
}

static PyObject *pyembed_register_ioport_read(PyObject *self, PyObject *args)
{
    pio_addr_t addr;
    int length;
    PyObject *callback;
    int ret;
    
    if (!PyArg_ParseTuple(args, "iiO", &addr, &length, &callback)) {
        PyErr_SetString(PyExc_TypeError, "invalid arguments");
        return NULL;
    }

    if (!PyCallable_Check(callback)) {
        PyErr_SetString(PyExc_TypeError, "parameter must be callable");
        return NULL;
    }

    Py_XINCREF(callback);

    opaque_data[addr].read = callback;

    ret = register_ioport_read(addr, length, 1,
                               pyembed_ioport_read_trampoline1, &opaque_data[addr]);
    ret = register_ioport_read(addr, length, 2,
                               pyembed_ioport_read_trampoline2, &opaque_data[addr]);
    ret = register_ioport_read(addr, length, 4,
                               pyembed_ioport_read_trampoline4, &opaque_data[addr]);

    if (ret == -1) {
        Py_XDECREF(callback);
    }

    return Py_BuildValue("i", ret);
}

static PyObject *pyembed_vmstate_get_all(PyObject *self, PyObject *args)
{
    PyObject *result;
    GSList *lst;

    result = PyDict_New();

    for (lst = vmstate_get_all(); lst; lst = lst->next) {
        VMState *vms = lst->data;
        PyObject *entry;
        VMStateField *field;
        gchar *name;

        if (vms->vmsd == NULL) {
            continue;
        }

        entry = PyDict_New();
        for (field = vms->vmsd->fields; field && field->name; field++) {
            PyObject *value = NULL;

            if (field->info == NULL) {
                continue;
            }

            if (strcmp(field->info->name, "int8") == 0) {
                value = PyInt_FromLong(*(int8_t *)(vms->object + field->offset));
            } else if (strcmp(field->info->name, "int16") == 0) {
                value = PyInt_FromLong(*(int16_t *)(vms->object + field->offset));
            } else if (strcmp(field->info->name, "int32") == 0) {
                value = PyInt_FromLong(*(int32_t *)(vms->object + field->offset));
            } else if (strcmp(field->info->name, "int64") == 0) {
                value = PyInt_FromLong(*(int64_t *)(vms->object + field->offset));
            } else if (strcmp(field->info->name, "uint8") == 0) {
                value = PyInt_FromLong(*(uint8_t *)(vms->object + field->offset));
            } else if (strcmp(field->info->name, "uint16") == 0) {
                value = PyInt_FromLong(*(uint16_t *)(vms->object + field->offset));
            } else if (strcmp(field->info->name, "uint32") == 0) {
                value = PyInt_FromLong(*(uint32_t *)(vms->object + field->offset));
            } else if (strcmp(field->info->name, "uint64") == 0) {
                value = PyInt_FromLong(*(uint64_t *)(vms->object + field->offset));
            }

            if (value) {
                PyDict_SetItemString(entry, field->name, value);
            }

        }

        name = g_strdup_printf("%s[%d]", vms->name, vms->instance_id);
        PyDict_SetItemString(result, name, entry);
        g_free(name);
    }


    return result;
}    

static PyMethodDef qemu_methods[] = {
    { "register_ioport_read", pyembed_register_ioport_read, METH_VARARGS,
      "Register to handle ioport reads" },
    { "register_ioport_write", pyembed_register_ioport_write, METH_VARARGS,
      "Register to handle ioport writes" },
    { "vmstate_get_all", pyembed_vmstate_get_all, METH_VARARGS,
      "Get VMState data for all devices" },
    { },
};

void python_init(void)
{
    Py_Initialize();

    Py_InitModule("qemu", qemu_methods);

    pyglib_init();
}

void python_load(const char *filename)
{
    PyObject *name;
    PyObject *module;

    name = PyString_FromString(filename);
    if (name == NULL) {
        return;
    }

    module = PyImport_Import(name);
    Py_DECREF(name);
}

void python_cleanup(void)
{
    Py_Finalize();
}
