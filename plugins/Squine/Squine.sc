Squine : UGen {
    *ar { | freq=440.0, clip=0.0, skew=0.0, sync=0.0, mul=1.0, add=0.0, iminsweep=0, initphase=1.25 |
        ^this.multiNew('audio', freq, clip, skew, sync, iminsweep, initphase).madd(mul, add)
	}
}
