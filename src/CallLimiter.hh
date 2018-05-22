#if !defined(CallLimiter_hh)
#define CallLimiter_hh

#include <chrono>
#include <utility>

/*!
 * \brief Limit calls for the specific Callable object to once
 * per interval. User of this object can call \sa Call() any time,
 * but the underneath next call is excecuted only if the configured
 * inteval is passed.
 */
template <class Callable>
class CallLimiter
{
public:
    /*!
     * \brief Create the limiter object for \param callable with call
     * interval \param interval. \param delayFirstCall is flag if the
     * the first real call should be postponed.
     */
    CallLimiter(Callable callable
        , std::chrono::milliseconds interval
        , bool delayFirstCall)
      : mCallable{std::move(callable)}
      , mInterval{std::move(interval)}
      , mNextCall{std::chrono::high_resolution_clock::now()}
    {
      if (delayFirstCall)
        mNextCall += interval;
    }

    /*!
     * \brief Execute the operator() on our callable object if the period
     * after previous passed.
     *
     * \note User of this object should call this method more often than
     * the period to achieve the desired behavior.
     */
    void Call()
    {
      auto now = std::chrono::high_resolution_clock::now();
      if (mNextCall <= now)
      {
        while (mNextCall < now)
          mNextCall += mInterval;
        static_cast<void>(mCallable());
      }
    }

private:
    Callable mCallable;
    const std::chrono::milliseconds mInterval;

    std::chrono::time_point<std::chrono::high_resolution_clock> mNextCall;
};

/*!
 * \brief Helper to get CallLimiter<T> object.
 */
template <class Callable, class Rep, class Period>
CallLimiter<Callable> getCallLimiter(Callable callable, std::chrono::duration<Rep, Period> interval, bool delayFirstCall)
{
  return CallLimiter<Callable>(std::move(callable), std::move(interval), delayFirstCall);
}

#endif
