# Release History for MDBX

---
## v0.3.8 [2025-02-20] Mahlon E. Smith <mahlon@martini.nu>

Build fixes:

- Don't fail during compilation on pre 0.13.x libmdbx versions.


---
## v0.3.7 [2025-02-06] Mahlon E. Smith <mahlon@martini.nu>

Minutiae:

 - Update for deprecations in 0.13.x libmdbx stable.


## v0.3.6 [2024-06-12] Mahlon E. Smith <mahlon@martini.nu>


Minutiae:

 - Fix build for Clang/FreeBSD.


---
## v0.3.5 [2024-03-24] Mahlon E. Smith <mahlon@martini.nu>

Add support for libmdbx 0.12.x.


Bugfixes:

 - Ensure transactions are closed if exceptions are raised.


---
## v0.3.4 [2022-12-21] Mahlon E. Smith <mahlon@martini.nu>

Bugfixes:

 - Don't use mdbx_replace() when not using the original value(s)


---
## v0.3.3 [2021-10-22] Mahlon E. Smith <mahlon@martini.nu>

Bugfixes:

 - Close any open transactions if serialization functions fail.
 - Don't use functions that assume NUL is the stream terminator.


Enhancements:

 - Add Loggability for debug output.


---
## v0.3.2 [2021-07-13] Mahlon E. Smith <mahlon@martini.nu>

Bugfixes:

 - Fix double memory allocation during initialization.
 - Make various ruby->c string allocations safe with the garbage collector.


Minutiae:

 - Raise exception if instantiating with invalid options.


---
## v0.3.1 [2021-05-16] Mahlon E. Smith <mahlon@martini.nu>

Bugfix:

 - #drop could potentially remove unintended data.  Yanked version
   v0.3.0.


---
## v0.3.0 [2021-04-09] Mahlon E. Smith <mahlon@martini.nu>

Enhancements:

 - Alter the behavior of #clear, so it doesn't destroy collection
   environments, but just empties them.

 - Add #drop, which explictly -does- destroy a collection environment.

 - Switching to a collection now automatically creates its environment.

 - Add include? and has_key?, for presence checks without allocating
   value memory or requiring deserialization.


Bugfixes:

 - Run all cursor methods through rb_protect, to ensure proper
   cursor cleanup in the event of an exception mid iteration.

 - Fix the block form of collections to support multiple scopes.


## v0.2.1 [2021-04-06] Mahlon E. Smith <mahlon@martini.nu>

Enhancement:

- Automatically stringify any argument to the collection() method.


## v0.2.0 [2021-03-19] Mahlon E. Smith <mahlon@martini.nu>

Enhancement:

- Support dup/clone.  This has limited use, as there can only
  be one open handle per process, but implemented in the interests
  of avoiding unexpected behavior.


## v0.1.1 [2021-03-14] Mahlon E. Smith <mahlon@martini.nu>

Bugfix:

- Don't inadvertantly close an open transaction while using hash convenience methods.


## v0.1.0 [2021-03-14] Mahlon E. Smith <mahlon@martini.nu>

Initial public release.


## v0.0.1 [2020-12-16] Mahlon E. Smith <mahlon@martini.nu>

Early release, initial basic functionality.


