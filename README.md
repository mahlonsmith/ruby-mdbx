
# Ruby::MDBX

**home**: https://code.martini.nu/fossil/ruby-mdbx

**docs**: https://martini.nu/docs/ruby-mdbx

**github**: https://github.com/mahlonsmith/ruby-mdbx


## Description

This is a Ruby (MRI) binding for the libmdbx database library.

libmdbx is an extremely fast, compact, powerful, embedded, transactional
key-value database, with a permissive license. libmdbx has a specific set
of properties and capabilities, focused on creating unique lightweight
solutions.

For more information about libmdbx (features, limitations, etc), see the
[introduction](https://libmdbx.dqdkfa.ru).


## Prerequisites

* Ruby 2.6+
* [libmdbx](https://gitflic.ru/project/erthink/libmdbx)


## Installation

    $ gem install mdbx

You may need to be specific if the libmdbx headers are located in a
nonstandard location for your operating system:

	$ gem install mdbx -- --with-opt-dir=/usr/local


## Usage

Some quick concepts:

  - A **database** is contained in a file, normally contained in directory
    with it's associated lockfile.
  - Each database can optionally contain multiple named **collections**,
    which can be thought of as distinct namespaces.
  - Each collection can contain any number of **keys**, and their associated
    **values**. 
  - A **snapshot** is a self-consistent read-only view of the database.
    It remains consistent even if another thread or process writes changes.
  - A **transaction** is a writable snapshot.  Changes made within a
    transaction are not seen by other snapshots until committed. 

### Open (and close) a database handle

Open a database handle, creating an empty one if not already present.

```ruby
db = MDBX::Database.open( "/path/to/file", options )
db.close
```

In block form, the handle is automatically closed.

```ruby
MDBX::Database.open( 'database' ) do |db|
    puts db[ 'key1' ]
end # closed database
```


### Read data

You can use the database handle as a hash. Reading a value automatically
creates a snapshot, retrieves the value, and closes the snapshot before
returning it.

```ruby
db[ 'key1' ] #=> val
```

All data reads require a snapshot (or transaction).

The `snapshot` method creates a long-running snapshot manually.  In
block form, the snapshot is automatically closed when the block exits.
Sharing a snapshot between reads is significantly faster when fetching
many values or in tight loops.

```ruby
# read-only block
db.snapshot do
    db[ 'key1' ] #=> val
    ...
end # snapshot closed
```

You can also open and close a snapshot manually.

```ruby
db.snapshot
db.values_at( 'key1', 'key2' ) #=> [ value, value ]
db.rollback
```

Technically, `snapshot` just sets the internal state and returns the
database handle - the handle is also yielded when using blocks.  The
following 3 examples are identical, use whatever form you prefer.

```ruby
snap = db.snapshot
snap[ 'key1' ]
snap.abort

db.snapshot do |snap|
    snap[ 'key1' ]
end

db.snapshot do
    db[ 'key1' ]
end
```

Attempting writes while within an open snapshot is an exception.


### Write data

Writing data is also hash-like.  Assigning a value to a key
automatically opens a writable transaction, stores the value, and
commits the transaction before returning.

All keys are strings, or converted to a string automatically.

```ruby
db[ 'key1' ] = val
db[ :key1 ] == db[ 'key1' ] #=> true
```

All data writes require a transaction.

The `transaction` method creates a long-running transaction manually.  In
block form, the transaction is automatically closed when the block exits.
Sharing a transaction between writes is significantly faster when
storing many values or in tight loops.

```ruby
# read/write block
db.transaction do
    db[ 'key1' ] = val
end # transaction committed and closed
```

You can also open and close a transaction manually.

```ruby
db.transaction
db[ 'key1' ] = val
db.commit
```

Like snapshots, `transaction` just sets the internal state and returns
the database handle - the handle is also yielded when using blocks.  The
following 3 examples are identical, use whatever form you prefer.

```ruby
txn = db.transaction
txn[ 'key1' ] = true
txn.save

db.transaction do |txn|
    txn[ 'key1' ] = true
end

db.transaction do
    db[ 'key1' ] = true
end
```

### Delete data

Just write a `nil` value to remove a key entirely, or like Hash, use the
`#delete` method:

```ruby
db[ 'key1' ] = nil
```

```ruby
oldval = db.delete( 'key1' )
```


### Transactions

Transactions are largely modelled after the
[Sequel](https://sequel.jeremyevans.net/rdoc/files/doc/transactions_rdoc.html)
transaction basics.

While in a transaction block, if no exception is raised, the
transaction is automatically committed and closed when the block exits.

```ruby
db[ 'key' ] = false

db.transaction do # BEGIN
    db[ 'key' ] = true
end # COMMIT

db[ 'key' ] #=> true
```

If the block raises a MDBX::Rollback exception, the transaction is
rolled back, but no exception is raised outside the block:

```ruby
db[ 'key' ] = false

db.transaction do # BEGIN
    db[ 'key' ] = true
    raise MDBX::Rollback
end # ROLLBACK

db[ 'key' ] #=> false
```

If any other exception is raised, the transaction is rolled back, and
the exception is raised outside the block:

```ruby
db[ 'key' ] = false

db.transaction do # BEGIN
    db[ 'key' ] = true
    raise ArgumentError
end # ROLLBACK

# ArgumentError raised
```


If you want to check whether you are currently in a transaction, use the
Database#in_transaction? method:

```ruby
db.in_transaction? #=> false
db.transaction do
    db.in_transaction? #=> true
end
```

MDBX writes are strongly serialized, and an open transaction blocks
other writers until it has completed.  Snapshots have no such
serialization, and readers from separate processes do not interfere with
each other.  Be aware of libmdbx behaviors while in open transactions.


### Collections

A MDBX collection is a sub-database, or a namespace.  In order to use
this feature, the database must be opened with the `max_collections`
option:

```ruby
db = MDBX::Database.open( "/path/to/file", max_collections: 10 )
```

Afterwards, you can switch collections at will.

```ruby
db.collection( 'sub' )
db.collection #=> 'sub'
db[ :key ] = true
db.main # switch to the top level
db[ :key ] #=> nil
```

In block form, the collection is reverted to the current collection when
the block was started:

```ruby
db.collection( 'sub1' )
db.collection( 'sub2' ) do
    db[ :key ] = true
end # the collection is reverted to 'sub1'
```

Collections cannot be switched while a snapshot or transaction is open.

Collection names are stored in the top-level database as keys.  Attempts
to use these keys as regular values, or switching to a key that is not
a collection will result in an incompatibility error.  While using
collections, It's probably wise to not store regular key/value data in a
top-level database to avoid this ambiguity.


### Value Serialization

By default, all values are stored as Marshal data - this is the most
"Ruby" behavior, as you can store any Ruby object directly that supports
`Marshal.dump`.

```ruby
db.serializer   = ->( v ) { Marshal.dump( v ) }
db.deserializer = ->( v ) { Marshal.load( v ) }
```

For compatibility with databases used by other languages, or if your
needs are more specific, you can disable or override the default
serialization behaviors after opening the database.

```ruby
# All values are JSON strings
db.serializer   = ->( v ) { JSON.generate( v ) }
db.deserializer = ->( v ) { JSON.parse( v ) }
```

```ruby
# Disable all automatic serialization
db.serializer   = nil
db.deserializer = nil
```

### Introspection

Calling `statistics` on a database handle will provide a subset of
information about the build environment, the database environment, and
the currently connected clients.


## Contributing

You can check out the current development source with Fossil via its
[home repo](https://code.martini.nu/fossil/ruby-mdbx), or with Git at its
[project mirror](https://gitlab.com/mahlon/ruby-mdbx).

After checking out the source, run:

    $ gem install -Ng
    $ rake setup

This will install dependencies, and do any other necessary setup for
development.


## Authors

- Mahlon E. Smith <mahlon@martini.nu>


## License

Copyright (c) 2020-2022 Mahlon E. Smith
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

