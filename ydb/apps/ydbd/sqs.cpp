#include "sqs.h"

#include <util/stream/file.h>

#include <library/cpp/logger/global/global.h>

using namespace NKikimr::NSQS;

class TFileEventsWriter : public IEventsWriterWrapper {
public:
    TFileEventsWriter(const TString& outputFileName)
        : OutputFile(TFile(outputFileName, OpenAlways | WrOnly))
        , OutStream(OutputFile)
    {}

    void Write(const TString& data) override
    {
        OutStream.Write(data);
        OutStream.Write("\n");
        OutStream.Flush();
        OutputFile.Flush();
    }
    void CloseImpl() noexcept override
    {
        OutStream.Flush();
        OutputFile.Flush();
        OutputFile.Close();
    }

private:
    TFile OutputFile;
    TFileOutput OutStream;
};


class TNullEventsWriter : public IEventsWriterWrapper {
public:
    TNullEventsWriter()
    {}

    void Write(const TString&) override
    {}

    void CloseImpl() noexcept override
    {}
};



IEventsWriterWrapper::TPtr TSqsEventsWriterFactory::CreateEventsWriter(const NKikimrConfig::TSqsConfig& config,  const NMonitoring::TDynamicCounterPtr&) const {
    const auto& ycSearchCfg = config.GetYcSearchEventsConfig();
    switch (ycSearchCfg.OutputMethod_case()) {
        case NKikimrConfig::TSqsConfig::TYcSearchEventsConfig::kOutputFileName:
            return new TFileEventsWriter(ycSearchCfg.GetOutputFileName());
        default:
            return new TNullEventsWriter();
    }
    return new TNullEventsWriter();
}
