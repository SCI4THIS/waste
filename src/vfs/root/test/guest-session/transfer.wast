;; Host upload/download yield parity: identical module and expected outcomes in
;; native (scripted replies), browser exports and the production worker.  Each
;; host-io yield is matched by exactly one scripted reply in FIFO order.
(module
  (import "waste_kernel" "host_upload_v1"
    (func $upload (param i32 i32 i32) (result i32)))
  (import "waste_kernel" "host_download_v1"
    (func $download (param i32 i32 i32 i32 i32) (result i32)))
  (import "env" "exit" (func $exit (param i32)))
  (memory (export "memory") 1)
  (data (i32.const 32) "/tmp/upload.bin")
  (data (i32.const 128) "download.bin")
  (data (i32.const 192) "ABCDEFGH")
  (func (export "__errno_location") (result i32) (i32.const 0))

  ;; Upload with scripted bytes supplied by the host.  Returns the number of
  ;; bytes committed to the kernel, which the contract pins at 8.
  (func (export "upload_ok") (result i32)
    (call $upload (i32.const 32) (i32.const 15) (i32.const 0)))

  ;; Second upload is cancelled by the host and must return -1 without
  ;; mutating the kernel.
  (func (export "upload_cancel") (result i32)
    (call $upload (i32.const 32) (i32.const 15) (i32.const 0)))

  ;; Download emits 8 bytes of payload; the host accepts and returns 0.
  (func (export "download_ok") (result i32)
    (call $download (i32.const 128) (i32.const 12)
                    (i32.const 192) (i32.const 8) (i32.const 0)))

  ;; Second download is cancelled and must return -1.
  (func (export "download_cancel") (result i32)
    (call $download (i32.const 128) (i32.const 12)
                    (i32.const 192) (i32.const 8) (i32.const 0)))

  (func (export "done") (call $exit (i32.const 0))))

(assert_return (invoke "upload_ok") (i32.const 8))
(assert_return (invoke "upload_cancel") (i32.const -1))
(assert_return (invoke "download_ok") (i32.const 0))
(assert_return (invoke "download_cancel") (i32.const -1))
(invoke "done")