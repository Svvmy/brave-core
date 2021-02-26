/* Copyright (c) 2020 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef BRAVE_COMPONENTS_TOR_TOR_CONTROL_H_
#define BRAVE_COMPONENTS_TOR_TOR_CONTROL_H_

#include <map>
#include <memory>
#include <queue>
#include <string>
#include <utility>
#include <vector>

#include "brave/components/tor/tor_control_event.h"

#include "base/callback.h"
#include "base/files/file_path.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "base/observer_list.h"
#include "base/process/process.h"
#include "base/time/time.h"
#include "brave/base/delete_soon_helper.h"

namespace base {
class FilePathWatcher;
class SequencedTaskRunner;
}  // namespace base

namespace net {
class DrainableIOBuffer;
class GrowableIOBuffer;
class TCPClientSocket;
}  // namespace net

namespace tor {

class TorControl : public base::DeleteSoonHelper<TorControl> {
 public:
  using PerLineCallback =
      base::RepeatingCallback<void(const std::string& status,
                                   const std::string& reply)>;
  using CmdCallback = base::OnceCallback<
      void(bool error, const std::string& status, const std::string& reply)>;

  class Delegate : public base::SupportsWeakPtr<Delegate> {
   public:
    virtual ~Delegate() = default;
    virtual void OnTorControlReady() = 0;
    virtual void OnTorControlClosed(bool was_running) = 0;

    virtual void OnTorEvent(
        TorControlEvent,
        const std::string& initial,
        const std::map<std::string, std::string>& extra) = 0;

    // Debugging options.
    virtual void OnTorRawCmd(const std::string& cmd) {}
    virtual void OnTorRawAsync(const std::string& status,
                               const std::string& line) {}
    virtual void OnTorRawMid(const std::string& status,
                             const std::string& line) {}
    virtual void OnTorRawEnd(const std::string& status,
                             const std::string& line) {}
  };

  explicit TorControl(TorControl::Delegate* delegate);
  virtual ~TorControl();

  void Start(std::vector<uint8_t> cookie, int port);
  void Stop();

  void DeleteSoonImpl() override;

  void Cmd1(const std::string& cmd, CmdCallback callback);
  void Cmd(const std::string& cmd,
           PerLineCallback perline,
           CmdCallback callback);

  void Subscribe(TorControlEvent event,
                 base::OnceCallback<void(bool error)> callback);
  void Unsubscribe(TorControlEvent event,
                   base::OnceCallback<void(bool error)> callback);

  void GetVersion(
      base::OnceCallback<void(bool error, const std::string& version)>
          callback);
  void GetSOCKSListeners(
      base::OnceCallback<void(bool error,
                              const std::vector<std::string>& listeners)>
          callback);

 protected:
  friend class TorControlTest;
  FRIEND_TEST_ALL_PREFIXES(TorControlTest, ParseQuoted);
  FRIEND_TEST_ALL_PREFIXES(TorControlTest, ParseKV);
  FRIEND_TEST_ALL_PREFIXES(TorControlTest, ReadLine);

  static bool ParseKV(const std::string& string,
                      std::string* key,
                      std::string* value);
  static bool ParseKV(const std::string& string,
                      std::string* key,
                      std::string* value,
                      size_t* end);
  static bool ParseQuoted(const std::string& string,
                          std::string* value,
                          size_t* end);

 private:
  bool running_;
  scoped_refptr<base::SequencedTaskRunner> owner_task_runner_;
  SEQUENCE_CHECKER(owner_sequence_checker_);

  scoped_refptr<base::SequencedTaskRunner> io_task_runner_;
  SEQUENCE_CHECKER(io_sequence_checker_);

  std::unique_ptr<net::TCPClientSocket> socket_;

  // Write state machine.
  std::queue<std::string> writeq_;
  bool writing_;
  scoped_refptr<net::DrainableIOBuffer> writeiobuf_;

  // Read state machine.
  std::queue<std::pair<PerLineCallback, CmdCallback>> cmdq_;
  bool reading_;
  scoped_refptr<net::GrowableIOBuffer> readiobuf_;
  int read_start_;  // offset where the current line starts
  bool read_cr_;    // true if we have parsed a CR

  // Asynchronous command response callback state machine.
  std::map<TorControlEvent, size_t> async_events_;
  struct Async {
    Async();
    ~Async();
    TorControlEvent event;
    std::string initial;
    std::map<std::string, std::string> extra;
    bool skip;
  };
  std::unique_ptr<Async> async_;

  TorControl::Delegate* delegate_;

  base::WeakPtrFactory<TorControl> weak_ptr_factory_{this};

  void OpenControl(int port, std::vector<uint8_t> cookie);
  void Connected(std::vector<uint8_t> cookie, int rv);
  void Authenticated(bool error,
                     const std::string& status,
                     const std::string& reply);

  void DoCmd(std::string cmd, PerLineCallback perline, CmdCallback callback);

  void GetVersionLine(std::string* version,
                      const std::string& status,
                      const std::string& line);
  void GetVersionDone(
      std::unique_ptr<std::string> version,
      base::OnceCallback<void(bool error, const std::string& version)> callback,
      bool error,
      const std::string& status,
      const std::string& reply);
  void GetSOCKSListenersLine(std::vector<std::string>* listeners,
                             const std::string& status,
                             const std::string& reply);
  void GetSOCKSListenersDone(
      std::unique_ptr<std::vector<std::string>> listeners,
      base::OnceCallback<
          void(bool error, const std::vector<std::string>& listeners)> callback,
      bool error,
      const std::string& status,
      const std::string& reply);

  void DoSubscribe(TorControlEvent event,
                   base::OnceCallback<void(bool error)> callback);
  void Subscribed(TorControlEvent event,
                  base::OnceCallback<void(bool error)> callback,
                  bool error,
                  const std::string& status,
                  const std::string& reply);
  void DoUnsubscribe(TorControlEvent event,
                     base::OnceCallback<void(bool error)> callback);
  void Unsubscribed(TorControlEvent event,
                    base::OnceCallback<void(bool error)> callback,
                    bool error,
                    const std::string& status,
                    const std::string& reply);
  std::string SetEventsCmd();

  // Notify delegate on UI thread
  void NotifyTorControlReady();
  void NotifyTorControlClosed();

  void NotifyTorEvent(TorControlEvent,
                      const std::string& initial,
                      const std::map<std::string, std::string>& extra);
  void NotifyTorRawCmd(const std::string& cmd);
  void NotifyTorRawAsync(const std::string& status, const std::string& line);
  void NotifyTorRawMid(const std::string& status, const std::string& line);
  void NotifyTorRawEnd(const std::string& status, const std::string& line);

  void StartWrite();
  void DoWrites();
  void WriteDoneAsync(int rv);
  void WriteDone(int rv);

  void StartRead();
  void DoReads();
  void ReadDoneAsync(int rv);
  void ReadDone(int rv);
  bool ReadLine(const std::string& line);

  void Error();

  TorControl(const TorControl&) = delete;
  TorControl& operator=(const TorControl&) = delete;
};

}  // namespace tor

#endif  // BRAVE_COMPONENTS_TOR_TOR_CONTROL_H_
