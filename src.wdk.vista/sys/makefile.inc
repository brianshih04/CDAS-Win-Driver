mofcomp: cvtestusb.bmf

cvtestusb.bmf: cvtestusb.mof
        mofcomp -B:cvtestusb.bmf cvtestusb.mof
        wmimofck cvtestusb.bmf





