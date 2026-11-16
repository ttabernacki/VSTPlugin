#pragma once

// MPE input (MIDI Polyphonic Expression, MMA/MIDI 1.0 MPE spec), no JUCE.
//
// Each note plays on its own member channel, so a channel's pitch bend, pressure and slide (CC74) belong
// to that one note. A master channel's messages apply to every note in its zone.
//   Lower zone: master channel 1, members 2 up. Upper zone: master channel 16, members 15 down.
//   Bend ranges: members 48 semitones, masters 2 (the spec's defaults, and what Ableton Live sends), or
//   whatever RPN 0 sets. The MPE Configuration Message (RPN 6 on a master) sets the zone's size and
//   resets its ranges to the defaults.
// Off (an ordinary keyboard), every channel is one instrument: any pitch bend bends everything by the
// master range, channel pressure is everyone's, and poly pressure is per key.
//
// Ableton Live sends exactly this from MPE clips, MPE controllers and its MPE editing tools: a note's
// channel gets its bend, pressure and slide just before the note-on, then as they move.

#include <algorithm>
#include <array>
#include <cstdint>

namespace mpe
{
struct Channel
{
    double bend = 0;      // -1..1
    double pressure = 0;  // 0..1
    double timbre = 0.5;  // CC74, 0..1 (0.5 when it hasn't been sent)
    int rpnMsb = 127, rpnLsb = 127, dataMsb = 0;
};

class Input
{
public:
    bool enabled = true;
    int lowerMembers = 15, upperMembers = 0; // with no MCM: channel 1 master, 2-16 members
    double lowerMemberRange = 48, lowerMasterRange = 2, upperMemberRange = 48, upperMasterRange = 2;
    double plainRange = 2; // MPE off: the pitch wheel's range

    void reset()
    {
        for (auto& c : ch_)
            c = {};
    }
    const Channel& channel (int ch) const { return ch_[(size_t) std::clamp (ch, 1, 16)]; }

    bool inLower (int ch) const { return lowerMembers > 0 && ch >= 1 && ch <= 1 + lowerMembers; }
    bool inUpper (int ch) const { return upperMembers > 0 && ch <= 16 && ch >= 16 - upperMembers; }
    bool isMaster (int ch) const { return enabled && ((lowerMembers > 0 && ch == 1) || (upperMembers > 0 && ch == 16)); }
    int masterOf (int ch) const { return inUpper (ch) && ! inLower (ch) ? 16 : 1; }

    // a note's bend in semitones: its own channel's (if a member) plus its zone master's
    double bendSemis (int ch) const
    {
        if (! enabled)
            return lastBend_ * plainRange;
        const bool upper = masterOf (ch) == 16;
        const double master = channel (upper ? 16 : 1).bend * (upper ? upperMasterRange : lowerMasterRange);
        if (isMaster (ch) || ! (inLower (ch) || inUpper (ch)))
            return master;
        return master + channel (ch).bend * (upper ? upperMemberRange : lowerMemberRange);
    }
    // a note's pressure: its own channel's, or the master's if that's stronger (a keyboard's channel pressure)
    double pressure (int ch, double polyForKey) const
    {
        if (! enabled)
            return std::max (lastPressure_, polyForKey);
        const double master = channel (masterOf (ch)).pressure;
        return std::max ({ isMaster (ch) ? 0.0 : channel (ch).pressure, master, polyForKey });
    }
    double timbre (int ch) const { return enabled ? channel (ch).timbre : lastTimbre_; }

    // --- incoming ---------------------------------------------------------------------------------------
    void pitchBend (int ch, double v) // -1..1
    {
        ch_[(size_t) std::clamp (ch, 1, 16)].bend = std::clamp (v, -1.0, 1.0);
        lastBend_ = std::clamp (v, -1.0, 1.0);
    }
    void channelPressure (int ch, double v)
    {
        ch_[(size_t) std::clamp (ch, 1, 16)].pressure = std::clamp (v, 0.0, 1.0);
        lastPressure_ = std::clamp (v, 0.0, 1.0);
    }
    // returns true if the controller was MPE's business (RPN data, CC74)
    bool controller (int ch, int cc, int value)
    {
        auto& c = ch_[(size_t) std::clamp (ch, 1, 16)];
        switch (cc)
        {
            case 74:
                c.timbre = value / 127.0;
                lastTimbre_ = c.timbre;
                return true;
            case 101: c.rpnMsb = value; return true;
            case 100: c.rpnLsb = value; return true;
            case 6:
                c.dataMsb = value;
                applyRpn (std::clamp (ch, 1, 16), c.rpnMsb, c.rpnLsb, value, 0);
                return c.rpnMsb != 127 || c.rpnLsb != 127;
            case 38:
                applyRpn (std::clamp (ch, 1, 16), c.rpnMsb, c.rpnLsb, c.dataMsb, value);
                return c.rpnMsb != 127 || c.rpnLsb != 127;
            case 121: // reset all controllers
                c.bend = c.pressure = 0;
                c.timbre = 0.5;
                return false;
            default: return false;
        }
    }

private:
    void applyRpn (int ch, int msb, int lsb, int dataMsb, int dataLsb)
    {
        if (msb == 0 && lsb == 0) // pitch bend sensitivity: semitones (+ cents)
        {
            const double range = dataMsb + dataLsb / 100.0;
            if (! enabled || ch == 1 || ch == 16)
                plainRange = range;
            if (ch == 1 && lowerMembers > 0) lowerMasterRange = range;
            else if (ch == 16 && upperMembers > 0) upperMasterRange = range;
            else if (inLower (ch)) lowerMemberRange = range;
            else if (inUpper (ch)) upperMemberRange = range;
        }
        else if (msb == 0 && lsb == 6 && (ch == 1 || ch == 16)) // MPE Configuration Message: a zone's size
        {
            const int n = std::clamp (dataMsb, 0, 15);
            if (ch == 1)
            {
                lowerMembers = n;
                lowerMemberRange = 48;
                lowerMasterRange = 2;
                upperMembers = std::min (upperMembers, 14 - n);
            }
            else
            {
                upperMembers = n;
                upperMemberRange = 48;
                upperMasterRange = 2;
                lowerMembers = std::min (lowerMembers, 14 - n);
            }
            if (lowerMembers > 0 || upperMembers > 0)
                enabled = true;
        }
    }
    std::array<Channel, 17> ch_ {}; // 1..16
    double lastBend_ = 0, lastPressure_ = 0, lastTimbre_ = 0.5;
};
} // namespace mpe
