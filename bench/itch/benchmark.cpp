#include <trader/providers/nasdaq/itch_handler.h>
#include <cstdint>
#include <stdexcept>
#include <string_view>

using namespace CppTrader::ITCH;

class Feed final : public ITCHHandler {
public:
    std::uint64_t result = 0;

private:
    template<class Message> bool consume(const Message &message) {
        // The feed consumer observes the whole decoded message, including type-specific fields.
        asm volatile("" : : "m"(message));
        result += message.Timestamp + message.StockLocate + message.TrackingNumber +
                  static_cast<unsigned char>(message.Type);
        return true;
    }
#define HANDLE(Type) bool onMessage(const Type##Message &message) override { return consume(message); }
    HANDLE(SystemEvent)
    HANDLE(StockDirectory)
    HANDLE(StockTradingAction)
    HANDLE(RegSHO)
    HANDLE(MarketParticipantPosition)
    HANDLE(MWCBDecline)
    HANDLE(MWCBStatus)
    HANDLE(IPOQuoting)
    HANDLE(AddOrder)
    HANDLE(AddOrderMPID)
    HANDLE(OrderExecuted)
    HANDLE(OrderExecutedWithPrice)
    HANDLE(OrderCancel)
    HANDLE(OrderDelete)
    HANDLE(OrderReplace)
    HANDLE(Trade)
    HANDLE(CrossTrade)
    HANDLE(BrokenTrade)
    HANDLE(NOII)
    HANDLE(RPII)
    HANDLE(LULDAuctionCollar)
#undef HANDLE
    bool onMessage(const UnknownMessage &) override { return false; }
};

std::uint64_t parse(std::string_view input) {
    Feed feed;
    while (!input.empty()) {
        if (input.size() < 2) throw std::runtime_error("truncated ITCH length");
        const auto size = (static_cast<unsigned char>(input[0]) << 8) |
                          static_cast<unsigned char>(input[1]);
        input.remove_prefix(2);
        // CppTrader's public API takes void* but only reads the message.
        if (!size || static_cast<std::size_t>(size) > input.size() ||
            !feed.ProcessMessage(const_cast<char *>(input.data()), size))
            throw std::runtime_error("invalid ITCH message");
        input.remove_prefix(size);
    }
    return feed.result;
}
