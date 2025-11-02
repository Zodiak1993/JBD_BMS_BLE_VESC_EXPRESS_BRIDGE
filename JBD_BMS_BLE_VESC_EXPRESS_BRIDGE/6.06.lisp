; ===============================
; ESP-NOW SoC Receiver for VESC
; Compatible with VESC 6.06
; Receives SoC as string (0–100) and updates VESC BMS (0.0–1.0)
; ===============================

(def cell-total-count 16)

; Ensure sender and receiver are on the same Wi-Fi channel
(wifi-set-chan 1)

(print "ESP-NOW Bridge Init...")
(print "MAC:" (get-mac-addr))
(print "Channel:" (wifi-get-chan))

; Start ESP-NOW
(esp-now-start)

; Set total cell count in BMS
(set-bms-val 'bms-cell-num cell-total-count)


; ===============================
; Process incoming ESP-NOW data
; ===============================
(defun proc-data (src des data rssi)
  (progn
    ; Convert incoming string to SoC fraction (0.0–1.0)
    (def soc-f (/ (str-to-f data) 100.0))

    ; Apply safety limits
    (if (< soc-f 0.0) (def soc-f 0.0))
    (if (> soc-f 1.0) (def soc-f 1.0))

    ; Update VESC BMS SoC
    (set-bms-val 'bms-soc soc-f)
    (send-bms-can)

    (print "SoC normalized:" soc-f)
    (print "RSSI:" rssi)
  )
)


; ===============================
; Event handler loop
; ===============================
(defun event-handler ()
  (loopwhile t
    (progn
      (recv
        ((event-esp-now-rx (? src) (? des) (? data) (? rssi))
          (proc-data src des data rssi))
        (_ nil)
      )
    )
  )
)

; Register and enable ESP-NOW receive events
(event-register-handler (spawn event-handler))
(event-enable 'event-esp-now-rx)
