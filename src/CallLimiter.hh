/*
 *      Copyright (c) 2018~now Palo Kisa <palo.kisa@gmail.com>
 *
 *  This Program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This Program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this addon; see the file COPYING.  If not, write to
 *  the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 *  http://www.gnu.org/copyleft/gpl.html
 *
 */

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
     * \return flag if underneath call was performed
     *
     * \note User of this object should call this method more often than
     * the period to achieve the desired behavior.
     */
    bool Call()
    {
      auto now = std::chrono::high_resolution_clock::now();
      if (mNextCall <= now)
      {
        while (mNextCall < now)
          mNextCall += mInterval;
        static_cast<void>(mCallable());
        return true;
      }
      return false;
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
