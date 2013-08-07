/** basic_filters.h                                 -*- C++ -*-
    Rémi Attab, 26 Jul 2013
    Copyright (c) 2013 Datacratic.  All rights reserved.

    Default pool of filters for a bid request object.

*/

#pragma once

#include "generic_filters.h"
#include "rtbkit/common/exchange_connector.h"

#include <array>
#include <unordered_map>
#include <unordered_set>
#include <mutex>


namespace RTBKIT {


/******************************************************************************/
/* FILTER PRIORITY                                                            */
/******************************************************************************/

struct Priority
{
    static constexpr unsigned ExchangePre   = 0x000000;

    static constexpr unsigned Creative      = 0x010000;

    static constexpr unsigned HourOfWeek    = 0x020000;
    static constexpr unsigned Segment       = 0x030000;

    static constexpr unsigned UrlRegex      = 0x040000;
    static constexpr unsigned LanguageRegex = 0x050000;
    static constexpr unsigned LocationRegex = 0x060000;

    static constexpr unsigned ExchangePost  = 0xFF0000;
};


/******************************************************************************/
/* HOUR OF WEEK FILTER                                                        */
/******************************************************************************/

struct HourOfWeekFilter : public FilterBaseT<HourOfWeekFilter>
{
    HourOfWeekFilter() { data.fill(ConfigSet()); }

    static constexpr const char* name = "hourOfWeek";
    unsigned priority() const { return Priority::HourOfWeek; }

    void addConfig(
            unsigned configIndex, const std::shared_ptr<AgentConfig>& config)
    {
        setConfig(configIndex, *config, true);
    }

    void removeConfig(
            unsigned configIndex, const std::shared_ptr<AgentConfig>& config)
    {
        setConfig(configIndex, *config, false);
    }

    void filter(FilterState& state) const
    {
        ExcCheckNotEqual(state.request.timestamp, Date(), "Null auction date");
        state.narrowConfigs(data[state.request.timestamp.hourOfWeek()]);
    }

private:

    void setConfig(unsigned configIndex, const AgentConfig& config, bool value)
    {
        const auto& bitmap = config.hourOfWeekFilter.hourBitmap;
        for (size_t i = 0; i < bitmap.size(); ++i) {
            if (!bitmap[i]) continue;
            data[i].set(configIndex, value);
        }
    }

    std::array<ConfigSet, 24 * 7> data;
};


/******************************************************************************/
/* SEGMENTS FILTER                                                            */
/******************************************************************************/

/** \todo weights and exchanges.
    \todo build on top of the IncludeExclude filter.

 */
struct SegmentsFilter : public FilterBaseT<SegmentsFilter>
{
    static constexpr const char* name = "segments";
    unsigned priority() const { return Priority::Segment; }

    void addConfig(
            unsigned configIndex, const std::shared_ptr<AgentConfig>& config)
    {
        setConfig(configIndex, *config, true);
    }

    void removeConfig(
            unsigned configIndex, const std::shared_ptr<AgentConfig>& config)
    {
        setConfig(configIndex, *config, false);
    }

    void filter(FilterState& state) const;


private:

    struct SegmentInfo
    {
        std::unordered_map<int, ConfigSet> intSet;
        std::unordered_map<std::string, ConfigSet> strSet;

        void set(unsigned configIndex, const SegmentList& segments, bool value);

        template<typename K>
        ConfigSet get(const std::unordered_map<K, ConfigSet>& m, K k) const
        {
            auto it = m.find(k);
            return it != m.end() ? it->second : ConfigSet();
        }

        ConfigSet get(int i, std::string str) const
        {
            return i >= 0 ? get(intSet, i) : get(strSet, str);
        }
    };

    struct SegmentFilter
    {
        SegmentInfo include;
        SegmentInfo exclude;
        ConfigSet emptyInclude;
        ConfigSet excludeIfNotPresent;

        void set(
                unsigned configIndex,
                const AgentConfig::SegmentInfo& segments,
                bool value);

        // \todo This is not quite right but will do for now.
        // Double check with the segment filter test for all the edge cases.
        ConfigSet filter(const SegmentList& segments) const;
    };

    void setConfig(unsigned configIndex, const AgentConfig& config, bool value);

    std::unordered_map<std::string, SegmentFilter> data;
    std::unordered_set<std::string> excludeIfNotPresent;
};


/******************************************************************************/
/* URL FILTER                                                                 */
/******************************************************************************/

struct UrlRegexFilter : public FilterBaseT<UrlRegexFilter>
{
    static constexpr const char* name = "UrlRegex";
    unsigned priority() const { return Priority::UrlRegex; }

    void addConfig(
            unsigned configIndex, const std::shared_ptr<AgentConfig>& config)
    {
        impl.addIncludeExclude(configIndex, config->urlFilter);
    }

    void removeConfig(
            unsigned configIndex, const std::shared_ptr<AgentConfig>& config)
    {
        impl.removeIncludeExclude(configIndex, config->urlFilter);
    }

    void filter(FilterState& state) const
    {
        state.narrowConfigs(impl.filter(state.request.url.toString()));
    }

private:
    typedef RegexFilter<boost::regex, std::string> BaseFilter;
    IncludeExcludeFilter<BaseFilter> impl;
};


/******************************************************************************/
/* LANGUAGE FILTER                                                            */
/******************************************************************************/

struct LanguageRegexFilter : public FilterBaseT<LanguageRegexFilter>
{
    static constexpr const char* name = "LanguageRegex";
    unsigned priority() const { return Priority::LanguageRegex; }

    void addConfig(
            unsigned configIndex, const std::shared_ptr<AgentConfig>& config)
    {
        impl.addIncludeExclude(configIndex, config->languageFilter);
    }

    void removeConfig(
            unsigned configIndex, const std::shared_ptr<AgentConfig>& config)
    {
        impl.removeIncludeExclude(configIndex, config->languageFilter);
    }

    void filter(FilterState& state) const
    {
        state.narrowConfigs(impl.filter(state.request.language.rawString()));
    }

private:
    typedef RegexFilter<boost::regex, std::string> BaseFilter;
    IncludeExcludeFilter<BaseFilter> impl;
};


/******************************************************************************/
/* LOCATION FILTER                                                            */
/******************************************************************************/

struct LocationRegexFilter : public FilterBaseT<LocationRegexFilter>
{
    static constexpr const char* name = "LocationRegex";
    unsigned priority() const { return Priority::LocationRegex; }

    void addConfig(
            unsigned configIndex, const std::shared_ptr<AgentConfig>& config)
    {
        impl.addIncludeExclude(configIndex, config->locationFilter);
    }

    void removeConfig(
            unsigned configIndex, const std::shared_ptr<AgentConfig>& config)
    {
        impl.removeIncludeExclude(configIndex, config->locationFilter);
    }

    void filter(FilterState& state) const
    {
        Utf8String location = state.request.location.fullLocationString();
        state.narrowConfigs(impl.filter(location));
    }

private:
    typedef RegexFilter<boost::u32regex, Utf8String> BaseFilter;
    IncludeExcludeFilter<BaseFilter> impl;
};


/******************************************************************************/
/* EXCHANGE FILTER                                                            */
/******************************************************************************/

/** The lock makes it next to impossible to do any kind of pre-processing. */
struct ExchangePreFilter : public IterativeFilter<ExchangePreFilter>
{
    static constexpr const char* name = "ExchangePre";
    unsigned priority() const { return Priority::ExchangePre; }

    bool filterConfig(FilterState& state, const AgentConfig& config) const
    {
        if (!state.exchange) return true;

        const void * exchangeInfo = nullptr;

        {
            std::lock_guard<ML::Spinlock> guard(config.lock);
            auto it = config.providerData.find(state.exchange->exchangeName());
            if (it == config.providerData.end()) return false;

            exchangeInfo = it->second.get();
        }

        return state.exchange->bidRequestPreFilter(
                state.request, config, exchangeInfo);
    }
};

struct ExchangePostFilter : public IterativeFilter<ExchangePostFilter>
{
    static constexpr const char* name = "ExchangePost";
    unsigned priority() const { return Priority::ExchangePost; }

    bool filterConfig(FilterState& state, const AgentConfig& config) const
    {
        if (!state.exchange) return true;

        const void * exchangeInfo = nullptr;

        {
            std::lock_guard<ML::Spinlock> guard(config.lock);
            auto it = config.providerData.find(state.exchange->exchangeName());
            if (it == config.providerData.end()) return false;

            exchangeInfo = it->second.get();
        }

        return state.exchange->bidRequestPostFilter(
                state.request, config, exchangeInfo);
    }
};


/******************************************************************************/
/* CREATIVE FILTER                                                            */
/******************************************************************************/

struct CreativeFilter : public FilterBaseT<CreativeFilter>
{
    static constexpr const char* name = "Creative";
    unsigned priority() const { return Priority::Creative; }


    void addConfig(
            unsigned configIndex, const std::shared_ptr<AgentConfig>& config)
    {
    }

    void removeConfig(
            unsigned configIndex, const std::shared_ptr<AgentConfig>& config)
    {
    }

    void filter(FilterState& state) const
    {
        if (!state.exchange) {
            state.narrowConfigs(ConfigSet());
            return;
        }
    }
};


} // namespace RTBKIT