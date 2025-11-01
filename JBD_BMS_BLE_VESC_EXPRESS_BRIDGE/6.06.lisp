; ===============================
; ESP-NOW SoC Receiver for VESC
; VESC 6.06 compatible
; Receives SoC as string and updates BMS (0.0–1.0)
; ===============================

(def cell-total-count 16)
(wifi-set-chan 1)             ; Stelle sicher, dass Sender und Empfänger auf 1 sind


(print "ESP-NOW Bridge Init...")
(print "MAC:" (get-mac-addr))
(print "Channel:" (wifi-get-chan))

(esp-now-start)
(set-bms-val 'bms-cell-num cell-total-count)

(defun proc-data (src des data rssi)
  (progn
    ; Direkt den SoC-Wert aus data verwenden
    (def soc-f (/ (str-to-f data) 100.0))

    ; Safety limits
    (if (< soc-f 0.0) (def soc-f 0.0))
    (if (> soc-f 1.0) (def soc-f 1.0))

    ; Update VESC BMS SoC
    (set-bms-val 'bms-soc soc-f)
    (send-bms-can)

    ; Debug output
    (print "Source:" src "RSSI:" rssi)
    (print "SoC normalized:" soc-f)
  )
)


(defun event-handler ()
    (loopwhile t
        (progn
            (recv
                ((event-esp-now-rx (? src) (? des) (? data) (? rssi)) (proc-data src des data rssi))
                (_ nil)
            );recv
                
        );progn
    );loopwhile t
);defun event-handler ()

(event-register-handler (spawn event-handler))
(event-enable 'event-esp-now-rx)
