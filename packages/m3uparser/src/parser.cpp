#include "mradio/m3u/parser.hpp"

#include <charconv>
#include <optional>
#include <system_error>
#include <utility>

namespace mradio::m3u {
namespace {

constexpr std::string_view kBom = "\xEF\xBB\xBF";
constexpr std::string_view kBlank = " \t\v\f\r";
constexpr std::string_view kExtinf = "EXTINF";
constexpr std::string_view kHeader = "EXTM3U";

// Trailing '\r' is in kBlank on purpose: playlists get edited on Windows and
// pasted from web pages, and a stray CR left on the end of a URL breaks the
// stream request in a way that is very hard to see in a terminal.
std::string_view trim(std::string_view s) noexcept
{
    const auto first = s.find_first_not_of(kBlank);
    if (first == std::string_view::npos) {
        return {};
    }
    const auto last = s.find_last_not_of(kBlank);
    return s.substr(first, last - first + 1);
}

// Reads the duration field of an #EXTINF line. Returns nullopt when the field
// is absent or is not a number; callers treat that as "unknown", which is the
// normal case for radio streams regardless.
std::optional<double> parse_duration(std::string_view field) noexcept
{
    if (field.empty()) {
        return std::nullopt;
    }
    double value = 0.0;
    const char* const end = field.data() + field.size();
    const auto [ptr, ec] = std::from_chars(field.data(), end, value);
    if (ec != std::errc{} || ptr != end) {
        return std::nullopt;
    }
    return value;
}

}  // namespace

Playlist parse(std::string_view text)
{
    Playlist out;

    if (text.starts_with(kBom)) {
        text.remove_prefix(kBom.size());
    }

    // Carried from an #EXTINF line down to the URI line that follows it.
    std::string pending_title;
    double pending_duration = -1.0;
    bool have_pending = false;
    std::size_t pending_line = 0;

    std::size_t pos = 0;
    std::size_t line_no = 0;

    while (pos <= text.size()) {
        const std::size_t newline = text.find('\n', pos);
        const std::size_t end = (newline == std::string_view::npos) ? text.size() : newline;
        const std::string_view line = trim(text.substr(pos, end - pos));
        ++line_no;
        pos = end + 1;

        const bool last_line = (newline == std::string_view::npos);

        if (!line.empty()) {
            if (line.front() != '#') {
                // A URI line closes whatever #EXTINF was pending.
                out.entries.push_back(Entry{
                    .title = std::move(pending_title),
                    .url = std::string(line),
                    .duration_seconds = pending_duration,
                });
                pending_title.clear();
                pending_duration = -1.0;
                have_pending = false;
            }
            else {
                const std::string_view body = line.substr(1);

                // Everything that is not exactly #EXTINF - the #EXTM3U header,
                // HLS directives, plain comments - carries nothing we need.
                std::string_view rest;
                bool is_extinf = false;
                if (body.starts_with(kExtinf)) {
                    rest = body.substr(kExtinf.size());
                    if (rest.empty()) {
                        is_extinf = true;
                    }
                    else if (rest.front() == ':') {
                        rest.remove_prefix(1);
                        is_extinf = true;
                    }
                }

                if (is_extinf) {
                    if (have_pending) {
                        out.problems.push_back(Problem{
                            pending_line,
                            "#EXTINF is not followed by a URI line; its title was dropped"});
                    }

                    // "#EXTINF:<duration>,<title>". Split on the FIRST comma
                    // only - station names such as "Jazz, Blues & Soul" are
                    // common and must survive whole.
                    std::string_view duration_field = trim(rest);
                    std::string_view title_field;
                    if (const auto comma = rest.find(','); comma != std::string_view::npos) {
                        duration_field = trim(rest.substr(0, comma));
                        title_field = trim(rest.substr(comma + 1));
                    }

                    const std::optional<double> duration = parse_duration(duration_field);
                    if (!duration && !duration_field.empty()) {
                        out.problems.push_back(Problem{
                            line_no,
                            "#EXTINF duration is not a number: '"
                                + std::string(duration_field) + "'"});
                    }

                    pending_duration = duration.value_or(-1.0);
                    pending_title = std::string(title_field);
                    pending_line = line_no;
                    have_pending = true;
                }
                else if (body != kHeader) {
                    // Nothing to do: comments and unmodelled directives are fine.
                }
            }
        }

        if (last_line) {
            break;
        }
    }

    if (have_pending) {
        out.problems.push_back(Problem{
            pending_line, "#EXTINF at end of file with no URI line after it"});
    }

    return out;
}

}  // namespace mradio::m3u
