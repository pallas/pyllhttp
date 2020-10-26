from setuptools import setup
from distutils.core import Extension

from os import path
this_directory = path.abspath(path.dirname(__file__))
with open(path.join(this_directory, 'README.md'), encoding='utf-8') as f:
    long_description = f.read()

setup(
    name = 'llhttp',
    version = '2.2.0.1',
    description = ("llhttp in python"),
    url = "http://github.com/pallas/pyllhttp",
    author = "Derrick Lyndon Pallas",
    author_email = "derrick@pallas.us",
    license = "MIT",
    long_description = long_description,
    long_description_content_type = "text/markdown",
    keywords = "www http parser",
    classifiers = [
        "Development Status :: 3 - Alpha",
        "Programming Language :: Python :: 3 :: Only",
        "Programming Language :: JavaScript",
        "Topic :: Software Development :: Libraries :: Python Modules",
        "Topic :: System :: Networking",
        "Topic :: Internet :: WWW/HTTP :: HTTP Servers",
        "License :: OSI Approved :: MIT License",
    ],
    ext_modules = [ Extension('llhttp', sources = """
        pyllhttp.c
        llhttp/llhttp.h
        llhttp/llhttp.c
        llhttp/http.c
        llhttp/api.c
    """.split()) ],
)
#