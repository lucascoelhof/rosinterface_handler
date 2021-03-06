#pragma once
#define IF_HANDLER_DIAGNOSTICS_INCLUDED
#include <memory>
#include <diagnostic_updater/publisher.h>
#include <message_filters/subscriber.h>
#include <ros/forwards.h>
#include <ros/node_handle.h>

namespace rosinterface_handler {
//! TopicDiagnostic does not clean up after itself. This wrapper does just that.
class TopicDiagnosticWrapper {
public:
    TopicDiagnosticWrapper(std::string name, diagnostic_updater::Updater& diag,
                           const diagnostic_updater::FrequencyStatusParam& freq,
                           const diagnostic_updater::TimeStampStatusParam& stamp)
            : updater_{diag}, diag_(std::move(name), diag, freq, stamp) {
    }
    TopicDiagnosticWrapper(TopicDiagnosticWrapper&& rhs) noexcept = delete;
    TopicDiagnosticWrapper& operator=(TopicDiagnosticWrapper&& rhs) noexcept = delete;
    TopicDiagnosticWrapper(const TopicDiagnosticWrapper& rhs) = delete;
    TopicDiagnosticWrapper& operator=(const TopicDiagnosticWrapper& rhs) = delete;

    ~TopicDiagnosticWrapper() {
        updater_.removeByName(diag_.getName()); // this is the line we actually need..
    }

    void tick() {
        diag_.tick();
    }

    void tick(const ros::Time& stamp) {
        diag_.tick(stamp);
    }

    const std::string& name() {
        return diag_.getName();
    }

private:
    diagnostic_updater::Updater& updater_;
    diagnostic_updater::TopicDiagnostic diag_;
};

//! Like a message_filters::Subscriber, but also manages diagnostics.
template <typename MsgT, typename SubscriberBase = message_filters::Subscriber<MsgT>>
class DiagnosedSubscriber : public SubscriberBase {
    static_assert(ros::message_traits::HasHeader<MsgT>::value,
                  "DiagnosedSubscriber can only be used on messages with a header!");
    using SubscriberT = SubscriberBase;
    using MsgPtrT = boost::shared_ptr<const MsgT>;

public:
    template <typename... Args>
    // NOLINTNEXTLINE(readability-identifier-naming)
    explicit DiagnosedSubscriber(diagnostic_updater::Updater& updater, Args&&... args)
            : SubscriberBase(std::forward<Args>(args)...), updater_{updater} {
        SubscriberT::registerCallback([this](const MsgPtrT& msg) { this->onMessage(msg); });
    }

    DiagnosedSubscriber& minFrequency(double minFrequency) {
        this->minFreq_ = minFrequency;
        return *this;
    }
    DiagnosedSubscriber& maxTimeDelay(double maxTimeDelay) {
        this->maxTimeDelay_ = maxTimeDelay;
        initDiagnostic(this->getTopic());
        return *this;
    }

    void subscribe(ros::NodeHandle& nh, const std::string& topic, uint32_t queueSize,
                   const ros::TransportHints& transportHints = ros::TransportHints(),
                   ros::CallbackQueueInterface* callbackQueue = nullptr) override {
        SubscriberT::subscribe(nh, topic, queueSize, transportHints, callbackQueue);
        initDiagnostic(topic);
    }

    void subscribe() override {
        SubscriberT::subscribe();
        initDiagnostic(this->getTopic());
    }

    void unsubscribe() override {
        SubscriberT::unsubscribe();
        initDiagnostic("");
    }

private:
    void onMessage(const MsgPtrT& msg) {
        diagnostic_->tick(msg->header.stamp);
    }

    void initDiagnostic(const std::string& name) {
        diagnostic_.reset();
        if (name.empty()) {
            return;
        }
        using namespace diagnostic_updater;
        diagnostic_ = std::make_unique<TopicDiagnosticWrapper>(name + " subscriber", updater_,
                                                               FrequencyStatusParam(&minFreq_, &maxFreq_, 0),
                                                               TimeStampStatusParam(0., maxTimeDelay_));
    }
    double minFreq_{0.};
    double maxFreq_{std::numeric_limits<double>::infinity()};
    double maxTimeDelay_{0.};
    diagnostic_updater::Updater& updater_;
    std::unique_ptr<TopicDiagnosticWrapper> diagnostic_;
};

//! Similar to diagnostic_updater::DiagnosedPublisher, but with less segfaults
template <typename MsgT>
class DiagnosedPublisher {
    static_assert(ros::message_traits::HasHeader<MsgT>::value,
                  "DiagnosedPublisher can only be used on messgaes with a header!");
    using Publisher = diagnostic_updater::DiagnosedPublisher<const MsgT>;
    using PublisherPtr = std::shared_ptr<Publisher>;

public:
    explicit DiagnosedPublisher(diagnostic_updater::Updater& updater) : updater_{&updater} {
    }

    DiagnosedPublisher& operator=(const ros::Publisher& publisher) {
        init(publisher);
        return *this;
    }

    void publish(const boost::shared_ptr<const MsgT>& message) {
        if (!!publisher_) {
            publisher_->publish(message);
        }
    }

    void publish(const MsgT& message) {
        if (!!publisher_) {
            publisher_->publish(message);
        }
    }

    DiagnosedPublisher& minFrequency(double minFrequency) {
        this->minFreq_ = minFrequency;
        return *this;
    }

    DiagnosedPublisher& maxTimeDelay(double maxTimeDelay) {
        this->maxTimeDelay_ = maxTimeDelay;
        if (!!publisher_) {
            init(publisher_->getPublisher());
        }
        return *this;
    }

    ros::Publisher publisher() const {
        if (!!publisher_) {
            return publisher_->getPublisher();
        }
        return ros::Publisher();
    }

    std::string getTopic() const {
        return publisher().getTopic();
    }

    uint32_t getNumSubscribers() const {
        return !publisher_ ? uint32_t() : publisher_->getPublisher().getNumSubscribers();
    }

private:
    void init(const ros::Publisher& publisher) {
        if (!!publisher_) {
            publisher_.reset();
        }
        auto publisherPtr =
            new Publisher(publisher, *updater_, diagnostic_updater::FrequencyStatusParam(&minFreq_, &maxFreq_, 0.),
                          diagnostic_updater::TimeStampStatusParam(0., maxTimeDelay_));
        auto deleter = [updater = updater_, name = publisherPtr->getName()](Publisher* pub) {
            updater->removeByName(name);
            delete pub;
        };
        publisher_ = PublisherPtr(publisherPtr, deleter);
    }
    diagnostic_updater::Updater* updater_{nullptr};
    double minFreq_{0.};
    double maxFreq_{std::numeric_limits<double>::infinity()};
    double maxTimeDelay_{0.};
    PublisherPtr publisher_;
};
} // namespace rosinterface_handler
