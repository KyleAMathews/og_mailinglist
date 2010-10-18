<?php
// $Id: phpmailer.class.inc,v 1.17.2.5 2010/07/07 12:41:38 smk Exp $

/**
 * @file
 * Implements the base PHPMailer for OGMailinglist class.
 */

/**
 * Base PHPMailer for Drupal implementation with support for SMTP keep-alive
 * and setting a custom Return-Path.
 */
class OGMailinglistPHPMailer extends PHPMailer {
  /**
   * Stores the addresses we want to deliver the message to.
   */
  private   $DeliverTo = array();

  /**
   * Stores the incoming message's date.
   */
  public $MessageDate = '';

  /**
   * Public wrapper around PHPMailer::RFCDate().
   */
  public function RFCDate() {
    return $this->MessageDate;
  }

  public function AddDeliverTo($address) {
    $this->DeliverTo[] = $address;
  }

  /**
   * Sends mail using the $Sendmail program.
   * @param string $header The message headers
   * @param string $body The message body
   * @access protected
   * @return bool
   */
  public function SendmailSendDeliverTo($header, $body) {
    if ($this->Sender != '') {
      $sendmail = sprintf("%s -oi -f %s %s", escapeshellcmd($this->Sendmail), escapeshellarg($this->Sender),
        escapeshellarg(implode(', ', $this->DeliverTo)));
    } else {
      $sendmail = sprintf("%s -oi %s", escapeshellcmd($this->Sendmail), escapeshellarg($this->DeliverTo));
    }
    if(!@$mail = popen($sendmail, 'w')) {
      throw new phpmailerException($this->Lang('execute') . $this->Sendmail, self::STOP_CRITICAL);
    }
    fputs($mail, $header);
    fputs($mail, $body);
    $result = pclose($mail);
    // implement call back function if it exists
    $isSent = ($result == 0) ? 1 : 0;
    $this->doCallback($isSent,$this->to,$this->cc,$this->bcc,$this->Subject,$body);
    if($result != 0) {
      throw new phpmailerException($this->Lang('execute') . $this->Sendmail, self::STOP_CRITICAL);
    }
    return true;
  }

  /**
   * Sends mail via SMTP using PhpSMTP
   * Returns false if there is a bad MAIL FROM, RCPT, or DATA input.
   * @param string $header The message headers
   * @param string $body The message body
   * @uses SMTP
   * @access protected
   * @return bool
   */
  protected function SmtpSendDeliverTo($header, $body) {
    require_once $this->PluginDir . 'class.smtp.php';
    $bad_rcpt = array();

    if(!$this->SmtpConnect()) {
      throw new phpmailerException($this->Lang('smtp_connect_failed'), self::STOP_CRITICAL);
    }
    $smtp_from = ($this->Sender == '') ? $this->From : $this->Sender;
    if(!$this->smtp->Mail($smtp_from)) {
      throw new phpmailerException($this->Lang('from_failed') . $smtp_from, self::STOP_CRITICAL);
    }

    // Attempt to send attach all recipients
    foreach($this->DeliverTo as $to) {
      if (!$this->smtp->Recipient($to)) {
        $bad_rcpt[] = $to[0];
        // implement call back function if it exists
        $isSent = 0;
        $this->doCallback($isSent,$to,'','',$this->Subject,$body);
      } else {
        // implement call back function if it exists
        $isSent = 1;
        $this->doCallback($isSent,$to,'','',$this->Subject,$body);
      }
    }

    if (count($bad_rcpt) > 0 ) { //Create error message for any bad addresses
      $badaddresses = implode(', ', $bad_rcpt);
      throw new phpmailerException($this->Lang('recipients_failed') . $badaddresses);
    }
    if(!$this->smtp->Data($header . $body)) {
      throw new phpmailerException($this->Lang('data_not_accepted'), self::STOP_CRITICAL);
    }
    if($this->SMTPKeepAlive == true) {
      $this->smtp->Reset();
    }
    return true;
  }

  /**
   * Creates message and assigns Mailer. If the message is
   * not sent successfully then it returns false.  Use the ErrorInfo
   * variable to view description of the error.
   * @return bool
   */
  public function Send() {
    if (count($this->DeliverTo) < 1) {
      return parent::Send();
    }

    try {
      // Set whether the message is multipart/alternative
      if(!empty($this->AltBody)) {
        $this->ContentType = 'multipart/alternative';
      }

      $this->error_count = 0; // reset errors
      $this->SetMessageType();
      $header = $this->CreateHeader();
      $body = $this->CreateBody();

      if (empty($this->Body)) {
        throw new phpmailerException($this->Lang('empty_message'), self::STOP_CRITICAL);
      }

      // digitally sign with DKIM if enabled
      if ($this->DKIM_domain && $this->DKIM_private) {
        $header_dkim = $this->DKIM_Add($header,$this->Subject,$body);
        $header = str_replace("\r\n","\n",$header_dkim) . $header;
      }

      // Choose the mailer and send through it
      switch($this->Mailer) {
        case 'sendmail':
          return $this->SendmailSendDeliverTo($header, $body);
        case 'smtp':
          return $this->SmtpSendDeliverTo($header, $body);
        default:
          return $this->MailSend($header, $body);
      }

    } catch (phpmailerException $e) {
      $this->SetError($e->getMessage());
      if ($this->exceptions) {
        throw $e;
      }
      echo $e->getMessage()."\n";
      return false;
    }
  }

}

