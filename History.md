# Release History for MDBX

---
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


