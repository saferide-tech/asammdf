#include <Python.h>
#include "numpy/arrayobject.h"
#include "numpy/ndarrayobject.h"
#include <stdio.h>
#include <stdbool.h>

#define PY_PRINTF(o) \
    PyObject_Print(o, stdout, 0); printf("\n");

char err_string[1024];

struct rec_info {
    unsigned long id;
    unsigned long size;
    PyObject* mlist;
}; 

struct node {
    struct node * next;
    struct rec_info info;
};




static PyObject* sort_data_block(PyObject* self, PyObject* args)
{
    unsigned long long id_size=0, position=0, size, tgt=10000;
    unsigned long rec_size, length, rec_id;
    PyObject *signal_data, *partial_records, *record_size, *optional, *mlist;
    PyObject *bts, *key, *value, *rem=NULL;
    unsigned char *buf, *end, *orig;
    struct node * head = NULL, *last=NULL, *item;
    
    if (!PyArg_ParseTuple(args, "OOOK|O", &signal_data, &partial_records, &record_size, &id_size, &optional))
    {
        snprintf(err_string, 1024, "sort_data_block was called with wrong parameters");
        PyErr_SetString(PyExc_ValueError, err_string);
        return 0;
    }
    else
    {
        Py_ssize_t pos = 0;
		position = 0;
       
        while (PyDict_Next(record_size, &pos, &key, &value)) 
        {
            item = malloc(sizeof(struct node));
            item->info.id = PyLong_AsUnsignedLong(key);
            item->info.size = PyLong_AsUnsignedLong(value);
            item->info.mlist = PyDict_GetItem(partial_records, key);
            item->next = NULL;
            if (last)
                last->next = item;
            if (!head)
                head = item;
            last = item; 
        }
 
        buf = (unsigned char *) PyBytes_AS_STRING(signal_data);
        orig = buf;
        size = (unsigned long long) PyBytes_GET_SIZE(signal_data);
        end = buf + size;
        
        while ((buf + id_size) < end)
        {
 
            rec_id = 0; 
            for (unsigned char i=0; i<id_size; i++, buf++) {
                rec_id += (*buf) << (i <<3);
            }  
            
            mlist = NULL;
            for (item=head; item!=NULL; item=item->next)
            {
                if (item->info.id == rec_id)
                {
                    rec_size = item->info.size;
                    mlist = item->info.mlist;
                    break;
                }
            }
            
            if (!mlist) {
                rem = PyBytes_FromStringAndSize(NULL, 0);
                return rem;
            }
            
            if (rec_size)
            {
                if (rec_size + position + id_size > size) {
                    break;
                }
                bts = PyBytes_FromStringAndSize((const char *)buf, (Py_ssize_t) rec_size);
                PyList_Append(
                    mlist,
                    bts
                );
                Py_DECREF(bts);

                buf += rec_size;

            }
            else
            {
                if (4 + position + id_size > size) {
                    break;
                }
                rec_size = (buf[3] << 24) + (buf[2] << 16) +(buf[1] << 8) + buf[0];
                length = rec_size + 4;
                if (position + length + id_size > size) {
                    break;
                }
                bts = PyBytes_FromStringAndSize((const char *)buf, (Py_ssize_t) length);
                PyList_Append(mlist, bts);
                Py_DECREF(bts);
                buf += length;
            }

            position = (unsigned long long) (buf - orig);
        } 
        
        while (head != NULL) {
            item = head;
            item->info.mlist = NULL;
     
            head = head->next;
            item->next = NULL;
            free(item);
        }
        
        head = NULL;
        last = NULL;
        item = NULL;
		mlist = NULL;
		
        rem = PyBytes_FromStringAndSize((const char *) (orig + position), (Py_ssize_t) (size - position));
		
		buf = NULL;
		orig = NULL;
		end = NULL;
	
        return rem;
    }
}

static Py_ssize_t calc_size(char* buf)
{
    return (unsigned char) buf[3] << 24 |
           (unsigned char) buf[2] << 16 |
           (unsigned char) buf[1] << 8 |
           (unsigned char) buf[0];
}

static PyObject* extract(PyObject* self, PyObject* args)
{
    int i=0, count, max=0, offset, list_count;
    Py_ssize_t pos=0, size=0;
    PyObject *signal_data, *is_byte_array, *offsets, *offsets_list;
    char *buf;
    PyArrayObject *vals;
    PyArray_Descr *descr;
    void *addr;
    unsigned char * addr2;

    if(!PyArg_ParseTuple(args, "OOO", &signal_data, &is_byte_array, &offsets))
    {
        snprintf(err_string, 1024, "extract was called with wrong parameters");
        PyErr_SetString(PyExc_ValueError, err_string);
        return 0;
    }
    else
    {
        Py_ssize_t max_size = 0;
        int retval = PyBytes_AsStringAndSize(signal_data, &buf, &max_size);
        
        count = 0;
        pos = 0;
        
        if (offsets == Py_None) {
            while ((pos + 4) < max_size)
            {
                size = calc_size(&buf[pos]);

                if (max < size) max = size;
                pos += 4 + size;
                count++;
            }
        }
        else {
            offsets_list = PyObject_CallMethod(offsets, "tolist", NULL);
            list_count = (int) PyList_Size(offsets_list);
            for (i=0; i<list_count; i++) {
                offset = (int) PyLong_AsLong(PyList_GET_ITEM(offsets_list, i));
                if ((offset + 4) >= max_size) break;
                size = calc_size(&buf[offset]);
                if (max < size) max = size;
                count++;
            }
        }

        if (PyObject_IsTrue(is_byte_array))
        {

            npy_intp dims[2];
            dims[0] = count;
            dims[1] = max;
            
            vals = (PyArrayObject *) PyArray_ZEROS(2, dims, NPY_UBYTE, 0);
            
            if (offsets == Py_None) {

                pos = 0;
                for (i=0; i<count; i++)
                {
                    addr2 = (unsigned char *) PyArray_GETPTR2(vals, i, 0);
                    size = calc_size(&buf[pos]);
                    pos += 4;
                    memcpy(addr2, &buf[pos], size);
                    pos += size;
                }
            }
            else {
                for (i=0; i<count; i++) {
                    addr2 = (unsigned char *) PyArray_GETPTR2(vals, i, 0);
                    offset = (int) PyLong_AsLong(PyList_GET_ITEM(offsets_list, i));
                    size = calc_size(&buf[offset]);
                    memcpy(addr2, &buf[offset+4], size);
                }
            }
        }
        else
        {
            npy_intp dims[1];
            dims[0] = count;

            descr = PyArray_DescrFromType(NPY_STRING);
            descr = PyArray_DescrNew(descr);
            descr->elsize = max;

            vals = (PyArrayObject *) PyArray_Zeros(1, dims, descr, 0);
            
            if (offsets == Py_None) {

                pos = 0;
                for (i=0; i<count; i++)
                {
                    addr2 = (unsigned char *) PyArray_GETPTR1(vals, i);
                    size = calc_size(&buf[pos]);
                    pos += 4;
                    memcpy(addr2, &buf[pos], size);
                    pos += size;
                }
            }
            else {
                for (i=0; i<count; i++) {
                    addr2 = (unsigned char *) PyArray_GETPTR1(vals, i);
                    offset = (int) PyLong_AsLong(PyList_GET_ITEM(offsets_list, i));
                    size = calc_size(&buf[offset]);
                    memcpy(addr2, &buf[offset+4], size);
                }
            }
        }
    }

    return (PyObject *) vals;
}


static PyObject* lengths(PyObject* self, PyObject* args)
{
    int i=0;
    Py_ssize_t count;
    int pos=0;
    PyObject *lst, *values, *item;

    if(!PyArg_ParseTuple(args, "O", &lst))
    {
        snprintf(err_string, 1024, "lengths was called with wrong parameters");
        PyErr_SetString(PyExc_ValueError, err_string);
        return 0;
    }
    else
    {

        count = PyList_Size(lst);

        values = PyTuple_New(count);

        for (i=0; i<(int)count; i++)
        {
            item = PyList_GetItem(lst, i);
            PyTuple_SetItem(values, i, PyLong_FromSsize_t(PyBytes_GET_SIZE(item)));
        }

    }

    return values;
}


static PyObject* get_vlsd_offsets(PyObject* self, PyObject* args)
{
    int i=0;
    Py_ssize_t count;
    int pos=0;
    PyObject *lst, *item, *result;
    npy_intp dim[1];
    PyArrayObject *values;

    unsigned long long current_size = 0;

    void *h_result;

    if(!PyArg_ParseTuple(args, "O", &lst))
    {
        snprintf(err_string, 1024, "get_vlsd_offsets was called with wrong parameters");
        PyErr_SetString(PyExc_ValueError, err_string);
        return 0;
    }
    else
    {

        count = PyList_Size(lst);
        dim[0] = (int) count;
        values = (PyArrayObject *) PyArray_SimpleNew(1, dim, NPY_ULONGLONG);

        for (i=0; i<(int) count; i++)
        {
            h_result = PyArray_GETPTR1(values, i);
            item = PyList_GetItem(lst, i);
            *((unsigned long long*)h_result) = current_size;
            current_size += (unsigned long long)PyBytes_GET_SIZE(item);
        }
    }

    result = PyTuple_Pack(2, values, PyLong_FromUnsignedLongLong(current_size));

    return result;
}


// Our Module's Function Definition struct
// We require this `NULL` to signal the end of our method
// definition
static PyMethodDef myMethods[] =
{
    { "extract", extract, METH_VARARGS, "extract VLSD samples from raw block" },
    { "lengths", lengths, METH_VARARGS, "lengths" },
    { "get_vlsd_offsets", get_vlsd_offsets, METH_VARARGS, "get_vlsd_offsets" },
    { "sort_data_block", sort_data_block, METH_VARARGS, "sort raw data group block" },
    
    { NULL, NULL, 0, NULL }
};

// Our Module Definition struct
static struct PyModuleDef cutils =
{
    PyModuleDef_HEAD_INIT,
    "cutils",
    "helper functions written in C for speed",
    -1,
    myMethods
};

// Initializes our module using our above struct
PyMODINIT_FUNC PyInit_cutils(void)
{
    import_array();
    return PyModule_Create(&cutils);
}
