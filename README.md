# Ruby MDBX

home
: https://code.martini.nu/ruby-mdbx

code
: https://code.martini.nu/ruby-mdbx

docs
: https://martini.nu/docs/ruby-mdbx

github
: https://github.com/mahlon/ruby-mdbx

gitlab
: https://gitlab.com/mahlon/ruby-mdbx

sourcehut:
: https://hg.sr.ht/~mahlon/ruby-mdbx


## Description

This is a Ruby (MRI) binding for the libmdbx database library.

libmdbx is an extremely fast, compact, powerful, embedded, transactional
key-value database, with permissive license. libmdbx has a specific set
of properties and capabilities, focused on creating unique lightweight
solutions.

  - Allows a swarm of multi-threaded processes to ACIDly read and update
  several key-value maps and multimaps in a locally-shared database.

  - Provides extraordinary performance, minimal overhead through
  Memory-Mapping and Olog(N) operations costs by virtue of B+ tree.

  - Requires no maintenance and no crash recovery since it doesn't use
  WAL, but that might be a caveat for write-intensive workloads with
  durability requirements.

  - Compact and friendly for fully embedding. Only ≈25KLOC of C11,
  ≈64K x86 binary code of core, no internal threads neither server
  process(es), but implements a simplified variant of the Berkeley DB
  and dbm API.

  - Enforces serializability for writers just by single mutex and
  affords wait-free for parallel readers without atomic/interlocked
  operations, while writing and reading transactions do not block each
  other.

  - Guarantee data integrity after crash unless this was explicitly
  neglected in favour of write performance.

  - Supports Linux, Windows, MacOS, Android, iOS, FreeBSD, DragonFly,
  Solaris, OpenSolaris, OpenIndiana, NetBSD, OpenBSD and other systems
  compliant with POSIX.1-2008.

  - Historically, libmdbx is a deeply revised and extended descendant
  of the amazing Lightning Memory-Mapped Database. libmdbx inherits
  all benefits from LMDB, but resolves some issues and adds a set of
  improvements.


### Examples

[forthcoming]


## Prerequisites

* Ruby 2.6+
* libmdbx (https://github.com/erthink/libmdbx)


## Installation

    $ gem install mdbx


## Contributing

You can check out the current development source with Mercurial via its
[home repo](https://code.martini.nu/ruby-mdbx), or with Git at its
[project page](https://gitlab.com/mahlon/ruby-mdbx).

After checking out the source, run:

    $ gem install -Ng
    $ rake setup

This will install dependencies, and do any other necessary setup for
development.


## Authors

- Mahlon E. Smith <mahlon@martini.nu>


## License

Copyright (c) 2020, Mahlon E. Smith
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

* Redistributions of source code must retain the above copyright notice,
  this list of conditions and the following disclaimer.

* Redistributions in binary form must reproduce the above copyright notice,
  this list of conditions and the following disclaimer in the documentation
  and/or other materials provided with the distribution.

* Neither the name of the author/s, nor the names of the project's
  contributors may be used to endorse or promote products derived from this
  software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

