(defun avg-latency (buf1 buf2)
  (set-buffer buf1)
  (let ((times1 (mapcar (lambda (x) (string-to-number x)) (split-string
							   (buffer-string)))))
    (set-buffer buf2)
    (let ((times2 (mapcar (lambda (x) (string-to-number x)) (split-string (buffer-string)))))
      (setq sum 0.0)
      (setq values (mapcar* (lambda (x y) (- x y)) times1 times2))
      (setq num-vals (length values))
      (while values
	(setq sum (+ sum (car values)))
	(setq values (cdr values)))
      (/ sum num-vals))))

(avg-latency "/ssh:compute17.fractus:/2_0" "/ssh:compute16.fractus:/2_0")
