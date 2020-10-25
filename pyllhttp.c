#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include <stdbool.h>
#include <string.h>
#include <ctype.h>
#include "llhttp/llhttp.h"

#define STRING(x) #x
#define XSTRING(x) STRING(x)
#define LLHTTP_VERSION XSTRING(LLHTTP_VERSION_MAJOR) "." XSTRING(LLHTTP_VERSION_MINOR) "." XSTRING(LLHTTP_VERSION_PATCH)

typedef struct {
    PyObject_HEAD
    llhttp_t llhttp;
} parser_object;

static PyObject *base_error;
static PyObject *errors[] = {
#define HTTP_ERRNO_GEN(CODE, NAME, _) NULL,
HTTP_ERRNO_MAP(HTTP_ERRNO_GEN)
#undef HTTP_ERRNO_GEN
};

static PyObject *methods[] = {
#define HTTP_METHOD_GEN(NUMBER, NAME, STRING) NULL,
HTTP_METHOD_MAP(HTTP_METHOD_GEN)
#undef HTTP_METHOD_GEN
};


static int
parser_callback(PyObject *type, llhttp_t *llhttp) {
    if (!PyObject_HasAttr(llhttp->data, type))
        return HPE_OK;

    PyObject *attr = PyObject_GetAttr(llhttp->data, type);
    if (attr) {
        PyObject *result = PyEval_CallObject(attr, NULL);
        if (result) {
            Py_DECREF(result);
        }
        Py_DECREF(attr);
    }

    if (PyErr_Occurred())
        return HPE_USER;

    if (HPE_PAUSED == llhttp_get_errno(llhttp)) {
        llhttp_resume(llhttp);
        return HPE_PAUSED;
    }

    if (HPE_PAUSED_UPGRADE == llhttp_get_errno(llhttp)) {
        llhttp_resume_after_upgrade(llhttp);
        return HPE_PAUSED_UPGRADE;
    }

    return HPE_OK;
}

static int
parser_data_callback(PyObject *type, llhttp_t *llhttp, const char *data, size_t length) {
    if (!PyObject_HasAttr(llhttp->data, type))
        return HPE_OK;

    PyObject *attr = PyObject_GetAttr(llhttp->data, type);
    if (attr) {
        PyObject *args = Py_BuildValue("(N)", PyMemoryView_FromMemory((char*)data, length, PyBUF_READ));
        if (args) {
            PyObject *result = PyEval_CallObject(attr, args);
            if (result) {
                Py_DECREF(result);
            }
            Py_DECREF(args);
        }
        Py_DECREF(attr);
    }

    if (PyErr_Occurred())
        return HPE_USER;

    if (HPE_PAUSED == llhttp_get_errno(llhttp)) {
        llhttp_resume(llhttp);
        return HPE_PAUSED;
    }

    if (HPE_PAUSED_UPGRADE == llhttp_get_errno(llhttp)) {
        llhttp_resume_after_upgrade(llhttp);
        return HPE_PAUSED_UPGRADE;
    }

    return HPE_OK;
}

#define PARSER_CALLBACK(type) \
static PyObject *string_ ## type; \
static int parser_ ## type (llhttp_t *llhttp) \
    { return parser_callback(string_ ## type, llhttp); }

#define PARSER_DATA_CALLBACK(type) \
static PyObject *string_ ## type; \
static int parser_ ## type (llhttp_t *llhttp, const char *data, size_t length) \
    { return parser_data_callback(string_ ## type, llhttp, data, length); }

PARSER_CALLBACK(on_message_begin)
PARSER_DATA_CALLBACK(on_url)
PARSER_DATA_CALLBACK(on_status)
PARSER_DATA_CALLBACK(on_header_field)
PARSER_DATA_CALLBACK(on_header_value)
PARSER_CALLBACK(on_headers_complete)
PARSER_DATA_CALLBACK(on_body)
PARSER_CALLBACK(on_message_complete)
PARSER_CALLBACK(on_chunk_header)
PARSER_CALLBACK(on_chunk_complete)

llhttp_settings_t parser_settings = {
    .on_message_begin = parser_on_message_begin,
    .on_url = parser_on_url,
    .on_status = parser_on_status,
    .on_header_field = parser_on_header_field,
    .on_header_value = parser_on_header_value,
    .on_headers_complete = parser_on_headers_complete,
    .on_body = parser_on_body,
    .on_message_complete = parser_on_message_complete,
    .on_chunk_header = parser_on_chunk_header,
    .on_chunk_complete = parser_on_chunk_complete,
};

static PyObject *
request_reset(PyObject *self) {
    llhttp_t *llhttp = &((parser_object*)self)->llhttp;
    llhttp_init(llhttp, HTTP_REQUEST, &parser_settings);
    llhttp->data = self;
    Py_RETURN_NONE;
}

static PyObject *
request_new(PyTypeObject *type, PyObject *args, PyObject *kwds) {
    PyObject *self = type->tp_alloc(type, 0);
    if (self)
        request_reset(self);
    return self;
}

static PyObject *
response_reset(PyObject *self) {
    llhttp_t *llhttp = &((parser_object*)self)->llhttp;
    llhttp_init(llhttp, HTTP_RESPONSE, &parser_settings);
    llhttp->data = self;
    Py_RETURN_NONE;
}

static PyObject *
response_new(PyTypeObject *type, PyObject *args, PyObject *kwds) {
    PyObject *self = type->tp_alloc(type, 0);
    if (self)
        response_reset(self);
    return self;
}

static PyObject *
parser_execute(PyObject *self, PyObject *payload) {
    llhttp_t *llhttp = &((parser_object*)self)->llhttp;

    Py_buffer buffer;
    if (PyObject_GetBuffer(payload, &buffer, PyBUF_SIMPLE))
        return NULL;

    if (!PyBuffer_IsContiguous(&buffer, 'C')) {
        PyErr_SetString(PyExc_TypeError, "buffer is not contiguous");
        PyBuffer_Release(&buffer);
        return NULL;
    }

    llhttp_errno_t error = llhttp_execute(llhttp, buffer.buf, buffer.len);
    PyBuffer_Release(&buffer);

    switch (error) {
    case HPE_OK:
        return PyLong_FromUnsignedLong(buffer.len);

    case HPE_PAUSED:
    case HPE_PAUSED_UPGRADE:
        return PyLong_FromUnsignedLong(llhttp->error_pos - (const char*)buffer.buf);

    case HPE_USER:
        return NULL;

    default:
        PyErr_SetString(errors[error], llhttp_get_error_reason(llhttp));
        return NULL;
    }
}

static PyObject *
parser_pause(PyObject *self) {
    llhttp_t *llhttp = &((parser_object*)self)->llhttp;
    llhttp_pause(llhttp);
    Py_RETURN_NONE;
}

static PyObject *
parser_unpause(PyObject *self) {
    llhttp_t *llhttp = &((parser_object*)self)->llhttp;
    llhttp_resume(llhttp);
    Py_RETURN_NONE;
}

static PyObject *
parser_upgrade(PyObject *self) {
    llhttp_t *llhttp = &((parser_object*)self)->llhttp;
    llhttp_resume_after_upgrade(llhttp);
    Py_RETURN_NONE;
}

static PyObject *
parser_finish(PyObject *self) {
    llhttp_t *llhttp = &((parser_object*)self)->llhttp;

    llhttp_errno_t error = llhttp_finish(llhttp);
    if (HPE_OK == error)
        Py_RETURN_NONE;

    PyErr_SetString(errors[error], llhttp_get_error_reason(llhttp));
    return NULL;
}

static PyObject * parser_reset(PyObject *self);

static PyMethodDef parser_methods[] = {
    { "execute", (PyCFunction)parser_execute, METH_O },
    { "pause", (PyCFunction)parser_pause, METH_NOARGS },
    { "unpause", (PyCFunction)parser_unpause, METH_NOARGS },
    { "upgrade", (PyCFunction)parser_upgrade, METH_NOARGS },
    { "finish", (PyCFunction)parser_finish, METH_NOARGS },
    { "reset", (PyCFunction)parser_reset, METH_NOARGS },
    { NULL }
};

static PyObject *
parser_method(PyObject *self, void *closure) {
    llhttp_t *llhttp = &((parser_object*)self)->llhttp;
    if (llhttp->type != HTTP_REQUEST)
        Py_RETURN_NONE;
    if (!llhttp->http_major && !llhttp->http_minor)
        Py_RETURN_NONE;

    PyObject * method = methods[llhttp->method];
    Py_INCREF(method);
    return method;
}

static PyObject *
parser_major(PyObject *self, void *closure) {
    llhttp_t *llhttp = &((parser_object*)self)->llhttp;
    if (!llhttp->http_major && !llhttp->http_minor)
        Py_RETURN_NONE;

    return PyLong_FromUnsignedLong(llhttp->http_major);
}

static PyObject *
parser_minor(PyObject *self, void *closure) {
    llhttp_t *llhttp = &((parser_object*)self)->llhttp;
    if (!llhttp->http_major && !llhttp->http_minor)
        Py_RETURN_NONE;

    return PyLong_FromUnsignedLong(llhttp->http_minor);
}

static PyObject *
parser_content_length(PyObject *self, void *closure) {
    llhttp_t *llhttp = &((parser_object*)self)->llhttp;
    if (!(llhttp->flags & F_CONTENT_LENGTH))
        Py_RETURN_NONE;

    return PyLong_FromUnsignedLong(llhttp->content_length);
}

static PyObject *
parser_get_lenient(PyObject *self, void *closure) {
    llhttp_t *llhttp = &((parser_object*)self)->llhttp;
    return PyBool_FromLong(llhttp->flags & F_LENIENT);
}

static int
parser_set_lenient(PyObject *self, PyObject *value, void *closure) {
    llhttp_t *llhttp = &((parser_object*)self)->llhttp;
    llhttp_set_lenient(llhttp, PyObject_IsTrue(value));
    return 0;
}

static PyObject *
parser_message_needs_eof(PyObject *self, void *closure) {
    llhttp_t *llhttp = &((parser_object*)self)->llhttp;
    return PyBool_FromLong(llhttp_message_needs_eof(llhttp));
}

static PyObject *
parser_should_keep_alive(PyObject *self, void *closure) {
    llhttp_t *llhttp = &((parser_object*)self)->llhttp;
    return PyBool_FromLong(llhttp_should_keep_alive(llhttp));
}

static PyObject *
parser_is_paused(PyObject *self, void *closure) {
    llhttp_t *llhttp = &((parser_object*)self)->llhttp;
    return PyBool_FromLong(HPE_PAUSED == llhttp_get_errno(llhttp));
}

static PyObject *
parser_is_upgrading(PyObject *self, void *closure) {
    llhttp_t *llhttp = &((parser_object*)self)->llhttp;
    return PyBool_FromLong(HPE_PAUSED_UPGRADE == llhttp_get_errno(llhttp));
}

static PyGetSetDef parser_getset[] = {
    { "method", parser_method },
    { "major", parser_major },
    { "minor", parser_minor },
    { "content_length", parser_content_length },
    { "lenient", parser_get_lenient, parser_set_lenient },
    { "message_needs_eof", parser_message_needs_eof },
    { "should_keep_alive", parser_should_keep_alive },
    { "is_paused", parser_is_paused },
    { "is_upgrading", parser_is_upgrading },
    { NULL }
};

static void
parser_dealloc(PyObject *self) {
    Py_TYPE(self)->tp_free((PyObject*)self);
}

static PyTypeObject request_type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "llhttp.Request",
    .tp_doc = "llhttp request parser",
    .tp_basicsize = sizeof(parser_object),
    .tp_getattro = PyObject_GenericGetAttr,
    .tp_setattro = PyObject_GenericSetAttr,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
    .tp_new = request_new,
    .tp_dealloc = parser_dealloc,
    .tp_methods = parser_methods,
    .tp_getset = parser_getset,
};

static PyTypeObject response_type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "llhttp.Response",
    .tp_doc = "llhttp response parser",
    .tp_basicsize = sizeof(parser_object),
    .tp_getattro = PyObject_GenericGetAttr,
    .tp_setattro = PyObject_GenericSetAttr,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
    .tp_new = response_new,
    .tp_dealloc = parser_dealloc,
    .tp_methods = parser_methods,
    .tp_getset = parser_getset,
};

static PyObject *
parser_reset(PyObject *self) {
    if (PyObject_TypeCheck(self, &request_type)) {
        request_reset(self);
        Py_RETURN_NONE;
    }

    if (PyObject_TypeCheck(self, &response_type)) {
        response_reset(self);
        Py_RETURN_NONE;
    }

    PyErr_SetString(PyExc_TypeError, "not llhttp.Request or llhttp.Response");
    return NULL;
}

static struct PyModuleDef llhttp_module = {
    PyModuleDef_HEAD_INIT,
    .m_name = "llhttp",
    .m_doc = "llhttp wrapper",
    .m_size = -1
};

static char *
snake_to_camel(char * string) {
    bool upper = true;
    char * camel = string;
    for (const char * snake = string ; *snake ; ++snake) {
        if (isalpha(*snake)) {
            *camel++ = upper ? toupper(*snake) : tolower(*snake);
        } else if (isdigit(*snake)) {
            *camel++ = *snake;
        }
        upper = !isalpha(*snake);
    }
    *camel = '\0';
    return string;
}

PyMODINIT_FUNC
PyInit_llhttp(void) {
    string_on_message_begin = PyUnicode_FromString("on_message_begin");
    string_on_url = PyUnicode_FromString("on_url");
    string_on_status = PyUnicode_FromString("on_status");
    string_on_header_field = PyUnicode_FromString("on_header_field");
    string_on_header_value = PyUnicode_FromString("on_header_value");
    string_on_headers_complete = PyUnicode_FromString("on_headers_complete");
    string_on_body = PyUnicode_FromString("on_body");
    string_on_message_complete = PyUnicode_FromString("on_message_complete");
    string_on_chunk_header = PyUnicode_FromString("on_chunk_header");
    string_on_chunk_complete = PyUnicode_FromString("on_chunk_complete");

    PyObject *m = PyModule_Create(&llhttp_module);
    if (!m)
        return NULL;

    if (PyModule_AddStringConstant(m, "version", LLHTTP_VERSION))
        goto fail;

    if ((base_error = PyErr_NewException("llhttp.Error", NULL, NULL))) {
        Py_INCREF(base_error);
        PyModule_AddObject(m, "Error", base_error);

        char *long_name = NULL;
#define HTTP_ERRNO_GEN(CODE, NAME, _) \
        if (CODE != HPE_OK && CODE != HPE_PAUSED && CODE != HPE_PAUSED_UPGRADE) \
        if ((long_name = strdup("llhttp." #NAME "_Error"))) { \
            char *short_name = snake_to_camel(long_name + strlen("llhttp.")); \
            if ((errors[CODE] = PyErr_NewException(long_name, base_error, NULL))) { \
                Py_INCREF(errors[CODE]); \
                PyModule_AddObject(m, short_name, errors[CODE]); \
            } \
            free(long_name); \
        }
HTTP_ERRNO_MAP(HTTP_ERRNO_GEN)
#undef HTTP_ERRNO_GEN

    }

#define HTTP_METHOD_GEN(NUMBER, NAME, STRING) \
    methods[HTTP_ ## NAME] = PyUnicode_FromStringAndSize(#STRING, strlen(#STRING));
HTTP_METHOD_MAP(HTTP_METHOD_GEN)
#undef HTTP_METHOD_GEN

    if (PyType_Ready(&request_type))
        goto fail;

    Py_INCREF(&request_type);
    if (PyModule_AddObject(m, request_type.tp_name + strlen("llhttp."), (PyObject *)&request_type)) {
        Py_DECREF(&request_type);
        goto fail;
    }

    if (PyType_Ready(&response_type))
        goto fail;

    Py_INCREF(&response_type);
    if (PyModule_AddObject(m, response_type.tp_name + strlen("llhttp."), (PyObject *)&response_type)) {
        Py_DECREF(&response_type);
        goto fail;
    }

    return m;

fail:
    Py_DECREF(m);
    return NULL;
}

//
