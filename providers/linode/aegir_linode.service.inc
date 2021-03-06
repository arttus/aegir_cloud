<?php

module_load_include('service.inc', 'aegir_cloud');


/**
 * linode provider.
 */
class hostingService_provider_linode extends hostingService_provider {
  public $type = 'linode';

  /**
   * The human readable name of this service.
   *
   * @var string
   */
  public $name = 'Linode';

  public $provider_server_url = 'https://control.linode.com/devices/details/%id%/virtualGuest';

  function form(&$form) {

    $options = variable_get('aegir_cloud_linode_options', array());
    if (empty($options)) {

      $form['warning'] = array(
        '#markup' => t('You must first configure your !link to use this provider.', array(
          '!link' => l(t('Linode Credentials'), 'admin/hosting/cloud'))
        ),
      );
      return;
    }

    parent::form($form);

    // Don't allow editing.
    if (!$this->server->nid) {
      $options = $this->getLinodeOptions();
      $form['provider_options'] = array();
      $form['provider_options']['datacenter'] = array(
        '#type' => 'select',
        '#title' => t('Datacenter'),
        '#options' => $options['datacenters'],
      );
      $form['provider_options']['memory'] = array(
        '#type' => 'select',
        '#title' => t('Memory'),
        '#options' => $options['memory'],
      );
      $form['provider_options']['operatingSystems'] = array(
        '#type' => 'select',
        '#title' => t('Operating System'),
        '#options' => $options['operatingSystems'],
      );
      $form['provider_options']['processors'] = array(
        '#type' => 'select',
        '#title' => t('Processors'),
        '#options' => $options['processors'],
      );
      $form['provider_options']['ssh_keys'] = array(
        '#type' => 'checkboxes',
        '#title' => t('SSH Keys'),
        '#options' => variable_get('aegir_cloud_linode_ssh_keys_options', array()),
        '#default_value' => array(
          variable_get('aegir_cloud_public_key_linode_id', ''),
        ),
      );
    }
  }

  /**
   * Validate the server form.
   * @param $node
   * @param $form
   */
  public function validate(&$node, &$form, &$form_state) {

    if (!empty($node->nid)) {
      return;
    }

    try {
      $api = $this->linode_client('Linode_Account');
      $account = $api->getObject();
      $this->node = $node;

      // Detect form errors.
      // If there are errors, do not attempt to create server.
      $errors = form_get_errors();
      if ($errors) {
        drupal_set_message(t('Validation errors must be fixed before we can create the server.'), 'error');
      }
      // If there are no errors, create the server.
      // We do this in the validation phase so that we can cause form errors if something goes wrong.
      else {

        // Create the Linode "Virtual Guest"
        $api = $this->linode_client('Linode_Virtual_Guest');
        $provider_data = $api->createObject($this->prepare_server_options());

        form_set_value($form['services']['provider']['linode']['provider_data'], $provider_data, $form_state);
        form_set_value($form['services']['provider']['linode']['provider_server_identifier'], $provider_data->id, $form_state);
      }

    } catch (Exception $e) {
      form_set_error('provider_options', $e->getMessage());
    }
  }

  /**
   * Server Node Insert.
   */
  function insert() {
    parent::insert();
  }

  /**
   * Prepares this server's options.
   *
   * Use the "Linode_Virtual_Guest" datatype reference:
   * http://sldn.linode.com/reference/datatypes/Linode_Virtual_Guest
   *
   * @return \stdClass
   */
  function prepare_server_options() {

    // Break up title into hostname (subdomain) and domain.
    $provider_options = new stdClass();
    $domain = explode('.', $this->node->title);
    $provider_options->hostname = $domain[0];
    $provider_options->domain = implode('.', array_slice($domain, 1));
    $provider_options->startCpus = $this->provider_options['processors'];
    $provider_options->maxMemory = $this->provider_options['memory'];
    $provider_options->hourlyBillingFlag = TRUE;
    $provider_options->localDiskFlag = TRUE;
    $provider_options->dedicatedAccountHostOnlyFlag = FALSE;
    $provider_options->operatingSystemReferenceCode = $this->provider_options['operatingSystems'];

    $provider_options->datacenter = new stdClass();
    $provider_options->datacenter->name = $this->provider_options['datacenter'];

    foreach (array_filter($this->provider_options['ssh_keys']) as $key_id) {
      $key = new stdClass();
      $key->id = $key_id;
      $provider_options->sshKeys[] = $key;
    }

    return $provider_options;
  }

  /**
   * Process options
   */
  function getLinodeOptions() {
    $options = array();
    $raw_options = variable_get('aegir_cloud_linode_options', array());

    if (empty($raw_options)) {
      return;
    }

    // Datacenters
    foreach ($raw_options->datacenters as $object) {
      $options['datacenters'][$object->template->datacenter->name] = $object->template->datacenter->name;
    }
    ksort($options['datacenters']);

    // Memory
    foreach ($raw_options->memory as $object) {
      $options['memory'][$object->template->maxMemory] = $object->itemPrice->item->description;
    }

    // OS
    foreach ($raw_options->operatingSystems as $object) {
      $options['operatingSystems'][$object->template->operatingSystemReferenceCode] = $object->itemPrice->item->description;
    }

    // CPUs
    foreach ($raw_options->processors as $object) {
      $options['processors'][$object->template->startCpus] = $object->itemPrice->item->description;
    }

    return $options;
  }

  /**
   * Helper for getting a linode client.
   * @param $service
   * @return \Linode_SoapClient
   */
  private function linode_client($service, $id = null) {
    $api_key = variable_get('aegir_cloud_linode_api_key');
    $username = variable_get('aegir_cloud_linode_api_username');

    // Initialize an API client for the Linode_Account service.
    require_once dirname(__FILE__) . '/linode-api-php-client/Linode/SoapClient.class.php';
    $client = Linode_SoapClient::getClient($service, $id, $username, $api_key);
    return $client;
  }

  public function context_options($task_type, $ref_type, &$task) {
    parent::context_options($task_type, $ref_type, $task);
    $task->context_options['linode_api_username'] = variable_get('aegir_cloud_linode_api_username','YOURUSERNAME');
    $task->context_options['linode_api_key'] = variable_get('aegir_cloud_linode_api_key','YOURKEY');
  }

  /**
   * Absolutely destroy this server using the Linode API.
   */
  public function destroy() {
    try {
      $vm_client = $this->linode_client('Linode_Virtual_Guest', $this->provider_server_identifier);
      $result = $vm_client->getBillingItem();

      $bill_client = $this->linode_client('Linode_Billing_Item', $result->id);
      $bill_client->cancelService();

      drupal_set_message(t('Linode server was destroyed.'));

    } catch(Exception $e) {
      form_set_error('provider', $e->getMessage());
      watchdog('aegir_cloud', 'CODE: ' . $e->getCode() . '|' . $e->getMessage());
    }
  }

  /**
   * Public method for getting a linode client.
   *
   * @param $service
   * @param $id
   * @return Linode_SoapClient
   */
  public function getClient($service, $id){
    return $this->linode_client($service, $id);
  }
}