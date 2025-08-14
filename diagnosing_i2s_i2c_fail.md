
| Test Description                        | Result                                                                                                               | Conclusion |
| --------------------------------------- | -------------------------------------------------------------------------------------------------------------------- | ---------- |
| Build but don't run pipeline            | fewer errors but eventually i2c fails and black on blue turns to white on blue with garbage display                  |            |
| Built and run pipeline without i2s node | Still got i2c logging errors, eventually (several minutes later) get garbage and transition to white on blue display |            |
| pipeline without i2sn without out even intializing it| no i2c errors, no garbage, no white on blue display| starting i2s master clock is sufficient to create i2c errors|
| build full pipeline but remove i2s lines between esp32 and es8388|||
|Wrap a ground around i2s wire bundle to shield it |some wraps seem to improve things, others don't||
        